# KatanaOpenAssetIO
# Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
# SPDX-License-Identifier: Apache-2.0
"""
Katana AssetWidgetDelegate plugin for KatanaOpenAssetIO.
"""

from Katana import UI4, AssetAPI, QT4FormWidgets, version

if version < (8,):
    from PyQt5 import QtCore, QtWidgets
else:
    from PySide6 import QtCore, QtWidgets


class OpenAssetIOBrowser(QtWidgets.QFrame):
    """
    An asset "browser", which is actually just a single text box.

    This is useful to allow asset IDs to be typed/pasted when browsing
    for assets, since asset IDs will be rejected by the default file
    browser, leaving no way to enter an asset ID in certain Katana
    inputs.

    In future this will be replaced by an adapter that proxies calls to
    an OpenAssetIO UI delegate plugin, if available.
    """
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

        QtWidgets.QVBoxLayout(self)

        self.__textbox = QT4FormWidgets.InputWidgets.InputLineEdit(self)
        self.layout().addWidget(self.__textbox, QtCore.Qt.AlignCenter)

    def setLocation(self, assetId):
        self.__textbox.setText(assetId)

    def getResult(self):
        return self.__textbox.text()

    def selectionValid(self):
        """
        Required method for asset browsers.

        @rtype: C{bool}
        @return: C{True} if the currently selected path is a valid
        selection, otherwise C{False}.
        """
        return True


class OpenAssetIOAssetRenderWidget(
    UI4.Util.AssetWidgetDelegatePlugins.DefaultAssetRenderWidget
):
    """
    Reproduce a similar UI to DefaultAssetRenderWidget but with logic
    to react to assets in "preflight" and a button to reset preflight
    status.

    See accompanying `KatanaOpenAssetIOPatches.py`, which patches the
    'Pre-'/'Post-Render Publish Asset' menu options to add/remove a
    `preflight` parameter. This parameter contains in-flight asset IDs
    that are used for querying asset metadata for publishing (i.e. the
    destination file path).

    This widget will trigger a re-sync of the parent RenderNodeEditor
    widget when we detect that the preflight parameter has changed.
    The re-sync will cause this widget to be destroyed and re-created.
    """

    def buildWidgets(self, hints):
        # Initialise to None since we need to lazily get the preflight
        # status (since we have no access to the parent RenderNodeEditor
        # widget here).
        self.__is_using_preflight_asset_id = None

        # Reproduce DefaultAssetRenderWidget.
        asset_id_widget = QtWidgets.QWidget(self)
        asset_id_hbox = QtWidgets.QHBoxLayout(asset_id_widget)
        asset_id_label = QtWidgets.QLabel("Output:", asset_id_widget)
        asset_id_label.setEnabled(False)
        asset_id_hbox.addWidget(asset_id_label, 0)
        self.__asset_id_label = QT4FormWidgets.InputWidgets.InputLineEdit(asset_id_widget)
        self.__asset_id_label.setReadOnly(True)
        asset_id_hbox.addWidget(self.__asset_id_label, 0)

        # Add a Reset button to destroy "preflight" state, i.e. if the
        # user has clicked 'Pre-Render Publish Asset' and wants to
        # abandon it.
        self.__reset_button = QtWidgets.QPushButton("Reset", self)
        self.__reset_button.clicked.connect(self.__on_reset_button_clicked)
        asset_id_hbox.addWidget(self.__reset_button, 0)

        self.layout().addWidget(asset_id_widget)
        self.updateWidgets()

    def updateWidgets(self):
        """
        Update widget state.

        This is called on construction, and when the parent widget state
        is updated, and when 'Pre-'/'Post-Render Publish Asset' is
        clicked.
        """
        # Get the render output info that this widget represents,
        # provided to this widget on construction.
        output_info = self.getOutputInfo()

        if self.__render_node_editor_widget() is not None:  # is None during initialisation.
            if self.__maybe_update_parent_if_preflight_state_changed():
                # This widget is now destroyed (and re-created), return
                # asap.
                return

        self.__reset_button.setHidden(not self.__is_using_preflight_asset_id)

        # "outputLocation" will either be a default (tmp) path; a path
        # from the upstream (RenderOutputDefine) settings; or a path
        # from the `preflight` parameter (see patched
        # `PreCreateProductAndLocation` and `RenderNodeInfo` in
        # `KatanaOpenAssetIOPatches.py`).
        output_location = output_info.get("outputLocation")
        if not output_location:
            output_location = "No output defined..."
        self.__asset_id_label.setText(output_location)

    def __maybe_update_parent_if_preflight_state_changed(self):
        output_info = self.getOutputInfo()
        output_name = output_info["name"]
        render_node_editor_widget = self.__render_node_editor_widget()

        # Detect if the (patched) 'Pre-Render Publish Asset' has
        # been executed but 'Post-Render Publish Asset' hasn't yet.
        is_using_preflight_asset_id = bool(
            render_node_editor_widget.getValuePolicy()
            .getNode()
            .getParameter(f"outputs.preflight.{output_name}")
        )

        if self.__is_using_preflight_asset_id is None:
            # Initialise. Can't do on construction since parent
            # (RenderNodeEditor) is not available then.
            self.__is_using_preflight_asset_id = is_using_preflight_asset_id
            return False

        if is_using_preflight_asset_id != self.__is_using_preflight_asset_id:
            # Render node needs re-syncing when preflight is
            # started/finished.
            render_node_editor_widget.manualSyncIncoming()
            # This widget is now destroyed.
            return True

        return False

    def __on_reset_button_clicked(self):
        render_node_editor_widget = self.__render_node_editor_widget()
        if not render_node_editor_widget:
            return

        # Retrieve parameter storing active "preflight" render asset
        # IDs, i.e. where the user has clicked 'Pre-Render Publish
        # Asset'.
        preflight_param = (
            render_node_editor_widget.getValuePolicy()
            .getNode()
            .getParameter(f"outputs.preflight")
        )
        if preflight_param is None:
            return

        # Get render output name associated with this widget.
        output_info = self.getOutputInfo()
        output_name = output_info.get("name")
        if output_name is None:
            return

        # Determine if this specific render output has been preflighted,
        # i.e. the user has clicked 'Pre-Render Publish Asset'.
        output_param = preflight_param.getChild(output_name)
        if not output_param:
            return

        # Reset the preflighted status for this render.
        preflight_param.deleteChild(output_param)

        # Now the parameter has been updated, the parent widget needs to
        # recalculate the render output info (which will result in this
        # widget being destroyed and re-created).
        render_node_editor_widget.manualSyncIncoming()

    def __render_node_editor_widget(self):
        return self.parent().parent().getFormWidgetParent()


class OpenAssetIOWidgetDelegate(
    UI4.Util.AssetWidgetDelegatePlugins.DefaultAssetWidgetDelegate
):

    def configureAssetBrowser(self, browser):
        UI4.Util.AssetWidgetDelegatePlugins.BaseAssetWidgetDelegate.configureAssetBrowser(
            self, browser
        )
        index = browser.addBrowserTab(OpenAssetIOBrowser, "Asset")

        browser.getBrowser(index).setLocation(str(self.getValuePolicy().getValue()))

    def createAssetRenderWidget(self, parent, outputInfo):
        w = OpenAssetIOAssetRenderWidget(parent, self.getWidgetHints(), outputInfo)
        parent.layout().addWidget(w)
        return w


PluginRegistry = [
    ("AssetWidgetDelegate", 1, "KatanaOpenAssetIO", OpenAssetIOWidgetDelegate),
]
