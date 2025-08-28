# KatanaOpenAssetIO
# Copyright (c) 2024-2025 The Foundry Visionmongers Ltd
# SPDX-License-Identifier: Apache-2.0
"""
Katana AssetWidgetDelegate plugin for KatanaOpenAssetIO.
"""
import logging
import pathlib
import sys

from Katana import UI4, AssetAPI, QT4FormWidgets, version

from openassetio import Context
from openassetio.hostApi import HostInterface, Manager
from openassetio.log import LoggerInterface
from openassetio.trait import TraitsData
from openassetio import errors

from openassetio.ui.access import UIAccess
from openassetio.ui.hostApi import UIDelegateRequestInterface, UIDelegateFactory, UIDelegate
from openassetio.ui.pluginSystem import (
    CppPluginSystemUIDelegateImplementationFactory,
    HybridPluginSystemUIDelegateImplementationFactory,
)
from openassetio.ui.pluginSystem.PythonPluginSystemUIDelegateImplementationFactory import (
    PythonPluginSystemUIDelegateImplementationFactory,
)
from openassetio_mediacreation.specifications.threeDimensional import SceneResourceSpecification

from openassetio_mediacreation.traits.application import ConfigTrait

from openassetio_mediacreation.traits.content import LocatableContentTrait
from openassetio_mediacreation.traits.uiPolicy import ManagedTrait
from openassetio_mediacreation.traits.identity import DisplayNameTrait
from openassetio_mediacreation.specifications.application import WorkfileSpecification
from openassetio_mediacreation.specifications.twoDimensional import (
    BitmapImageResourceSpecification,
)
from openassetio_mediacreation.traits.ui import (
    InlineTrait,
    EntityProviderTrait,
    SingularTrait,
    DetachedTrait,
    BrowserTrait,
    InPlaceTrait,
)

# Append our site-packages (traits) to the Python path.
# Assumes installation tree as configured in CMake.
sys.path.append(str(pathlib.Path(__file__).parent.parent / "site-packages"))

import katana_openassetio


if version < (8,):
    from PyQt5 import QtCore, QtWidgets
else:
    from PySide6 import QtCore, QtWidgets

HintTrue = QT4FormWidgets.HintUtils.HintTrue

plugin_id = "KatanaOpenAssetIO"

# Map of file extensions to MIME types.
#
# This reflects the C++ mapping in PublishStrategies.cpp.
_ext_to_mime = {
    "exr": "image/x-exr",  # From xdg/shared-mime-info
    "deepexr": "image/x-exr",  # Assume same as .exr
    "png": "image/png",  # From iana.org
    "tif": "image/tiff",  # From iana.org
    "jpg": "image/jpeg",  # From iana.org
    "rla": "image/x-rla",  # Unofficial
    "dtex": "image/x-dtex",  # Invented
    "deepshad": "image/x-deepshad",  # Invented
    "hist": "application/vnd.foundry.katana.histogram+xml",  # Invented
}

logger = logging.getLogger("KatanaOpenAssetIO")


class KatanaUIHostInterface(HostInterface):

    def displayName(self):
        return "Katana UI"

    def identifier(self):
        return "com.foundry.katana.ui"


class KatanaUILoggerInterface(LoggerInterface):

    _to_python_level = {
        LoggerInterface.Severity.kDebugApi: logging.DEBUG,
        LoggerInterface.Severity.kDebug: logging.DEBUG,
        LoggerInterface.Severity.kProgress: logging.INFO,
        LoggerInterface.Severity.kInfo: logging.INFO,
        LoggerInterface.Severity.kWarning: logging.WARNING,
        LoggerInterface.Severity.kError: logging.ERROR,
        LoggerInterface.Severity.kCritical: logging.CRITICAL,
    }

    def __init__(self):
        LoggerInterface.__init__(self)
        self.__python_logger = logger

    def isSeverityLogged(self, severity):
        return self.__python_logger.isEnabledFor(self._to_python_level[severity])

    def log(self, severity, message):
        self.__python_logger.log(self._to_python_level[severity], message)


class UIDelegateService:
    """
    Provide access to OpenAssetIO UI delegate, manager and context.

    OpenKatanaAssetIOWidgetDelegate is often (re-)constructed for
    one-off operations, so we need a singleton to avoid repeatedly
    scanning for plugins.
    """

    def __init__(self):
        self.__ui_delegate: UIDelegate = NotImplemented  # Sentinel for lazy-loading.
        self.__manager: Manager | None = None
        self.__context: Context | None = None

    def reset_manager(self):
        self.__manager = None
        self.__context = None

    def ui_delegate(self):
        if self.__ui_delegate is not NotImplemented:
            return self.__ui_delegate

        host_logger = KatanaUILoggerInterface()

        # If no config file (or importlib entry point) is discovered,
        # then defaultUIDelegateForInterface() will return None.
        #
        # However, if there is a config file, then it will try to load
        # the corresponding UI delegate plugin, and raise an
        # InputValidationException if not found.
        #
        # Ideally this would be an error that we would propagate to the
        # user. However, the config file for manager plugins and UI
        # delegate plugins is the same. So if only a manager plugin is
        # available, then we'll get an InputValidationException here.
        # Best we can do is log and return None in this case.
        try:
            self.__ui_delegate = UIDelegateFactory.defaultUIDelegateForInterface(
                KatanaUIHostInterface(),
                HybridPluginSystemUIDelegateImplementationFactory(
                    [
                        CppPluginSystemUIDelegateImplementationFactory(host_logger),
                        PythonPluginSystemUIDelegateImplementationFactory(host_logger),
                    ],
                    host_logger,
                ),
                host_logger,
            )
        except errors.InputValidationException as exc:
            host_logger.debug(f"Failed to load OpenAssetIO UI delegate plugin: {exc}")
            self.__ui_delegate = None
        except Exception as exc:
            host_logger.error(f"Failed to load OpenAssetIO UI delegate plugin: {exc}")
            self.__ui_delegate = None

        return self.__ui_delegate

    def manager(self):
        return self.manager_and_context()[0]

    def context(self):
        return self.manager_and_context()[1]

    def manager_and_context(self):
        if self.__manager is not None and self.__context is not None:
            return self.__manager, self.__context

        assetapi_plugin = AssetAPI.GetAssetPlugin(plugin_id)
        if assetapi_plugin is None:
            return None, None

        result = {}
        assetapi_plugin.runAssetPluginCommand(
            "",
            "setManagerAndContextInPythonDict",
            {"outDictId": str(id(result))},
            throwOnError=True,
        )
        self.__manager = result["manager"]
        self.__context = result["context"]

        return self.__manager, self.__context


def exception_logger_decorator(func):
    """
    Decorator to log exceptions in OpenKatanaAssetIOWidgetDelegate
    methods.
    """

    def wrapper(*args, **kwargs):
        try:
            return func(*args, **kwargs)
        except Exception as exc:
            logger.exception(exc)
            raise

    return wrapper


class OpenKatanaAssetIOWidgetDelegate(
    UI4.Util.AssetWidgetDelegatePlugins.DefaultAssetWidgetDelegate
):
    def __init__(self, *args, ui_service=UIDelegateService(), **kwargs):
        super().__init__(*args, **kwargs)
        self.__ui_service = ui_service
        logger.debug(f"Configuring widget delegate for widget hints:\n{self.getWidgetHints()}")
        self.__ui_service.reset_manager()

    @exception_logger_decorator
    def createAssetControlWidget(self, parent):
        # Check if UI delegate is available. If not, use the base class.
        if self.__ui_service.ui_delegate() is None:
            return super().createAssetControlWidget(parent)

        # Traits for type of UI that we want.

        ui_traits = TraitsData()
        # We want an inline widget.
        InlineTrait.imbueTo(ui_traits)
        # We want entities.
        EntityProviderTrait.imbueTo(ui_traits)
        # We want a single entity.
        SingularTrait.imbueTo(ui_traits)
        # We'll place the widget in the hierarchy ourselves.
        DetachedTrait.imbueTo(ui_traits)

        # Access mode - read vs. write

        ui_access = UIAccess.kRead

        # Check if the plugin can provide a delegated UI. Use base class
        # implementation if not.

        ui_policy = self.__ui_service.ui_delegate().uiPolicy(
            ui_traits.traitSet(), ui_access, self.__ui_service.context()
        )
        if not ManagedTrait.isImbuedTo(ui_policy):
            return super().createAssetControlWidget(parent)

        w = OpenKatanaAssetIOControlWidget(
            self.__ui_service, ui_traits, ui_access, parent, self.getWidgetHints()
        )
        parent.layout().addWidget(w)
        return w

    @exception_logger_decorator
    def configureAssetBrowser(self, browser):
        UI4.Util.AssetWidgetDelegatePlugins.BaseAssetWidgetDelegate.configureAssetBrowser(
            self, browser
        )

        if self.__ui_service.ui_delegate() is None:
            # If no OpenAssetIO UI delegate is available, then use a
            # default browser, which is a simple text box for entering
            # asset IDs.
            index = browser.addBrowserTab(SimpleTextBoxBrowser, "Asset")
            browser.getBrowser(index).setLocation(str(self.getValuePolicy().getValue()))
            return

        # Traits for type of UI that we want.

        ui_traits = TraitsData()
        # We want a browser.
        BrowserTrait.imbueTo(ui_traits)
        # We're browsing for entities.
        EntityProviderTrait.imbueTo(ui_traits)
        # We want a single entity.
        SingularTrait.imbueTo(ui_traits)
        # We want to mutate an existing container (we'll construct a
        # blank container for the OpenAssetIO delegate to mutate).
        InPlaceTrait.imbueTo(ui_traits)

        # Access mode - read vs. write

        value_policy = self.getValuePolicy()
        widget_hints = value_policy.getWidgetHints()

        isSave = (
            HintTrue("saveMode", widget_hints) or widget_hints.get("widget") == "assetIdOutput"
        )
        if isSave:
            ui_access = UIAccess.kWrite
        else:
            ui_access = UIAccess.kRead

        # Check if the plugin can provide a delegated UI. Short-circuit
        # if not

        ui_policy = self.__ui_service.ui_delegate().uiPolicy(
            ui_traits.traitSet(), ui_access, self.__ui_service.context()
        )
        if not ManagedTrait.isImbuedTo(ui_policy):
            return

        # Figure out what kind of entity to browse for.

        if widget_hints.get("context") == "katana scene context":
            spec = WorkfileSpecification.create()
            entity_traits = spec.traitsData()
            katana_openassetio.traits.application.ProjectTrait.imbueTo(entity_traits)
        elif widget_hints.get("context") == "look file context":
            spec = SceneResourceSpecification.create()
            entity_traits = spec.traitsData()
            katana_openassetio.traits.application.LookFileTrait.imbueTo(entity_traits)
        elif widget_hints.get("context") == "live group context":
            spec = WorkfileSpecification.create()
            entity_traits = spec.traitsData()
            katana_openassetio.traits.nodes.LiveGroupTrait.imbueTo(entity_traits)
            ConfigTrait.imbueTo(entity_traits)
        # Special case for RenderOutputDefine.
        elif value_policy.getFullName().endswith(
            "renderSettings.outputs.outputName.locationSettings.renderLocation"
        ):
            spec = BitmapImageResourceSpecification.create()
            entity_traits = spec.traitsData()
            extension = (
                value_policy.getParent()
                .getParent()
                .getChildByName("rendererSettings")
                .getChildByName("fileExtension")
                .getValue()
            )
            if extension:
                # Set the MIME type based on the file extension.
                mime_type = _ext_to_mime.get(extension, f"image/{extension}")
                LocatableContentTrait(entity_traits).setMimeType(mime_type)
        else:
            entity_traits = TraitsData()
            LocatableContentTrait.imbueTo(entity_traits)

        # Convert file types to MIME types.

        if file_types_str := widget_hints.get("fileTypes"):
            file_types = file_types_str.split("|")
        else:
            file_types = []

        mime_types = [
            {
                "katana": "application/vnd.foundry.katana.project",  # Invented.
                "klf": "application/vnd.foundry.katana.lookfile",  # Invented.
                "usda": "model/vnd.usda",  # IANA.
                "usdz": "model/vnd.usdz+zip",  # IANA.
                "usd": "model/vnd.usd",  # Invented (interestingly not in IANA).
            }.get(file_type, "application/octet-stream")
            for file_type in file_types
        ]

        if HintTrue("acceptDir", widget_hints):
            mime_types.append("inode/directory")

        if mime_types:
            LocatableContentTrait(entity_traits).setMimeType(",".join(mime_types))

        # Pass along any default entity/location.

        entity_refs = []
        if ref_or_path := value_policy.getValue():
            if entity_ref := self.__ui_service.manager().createEntityReferenceIfValid(ref_or_path):
                entity_refs.append(entity_ref)
            else:
                LocatableContentTrait(entity_traits).setLocation(ref_or_path)

        # Construct a blank container widget, which exposes the API
        # that Katana's widget delegate system expects.
        container = OpenKatanaAssetIOBrowserFrame()

        # UI state callback to update container widget's selected
        # value, which will be queried when OK is clicked on the
        # parent widget.
        def state_changed_cb(ui_state):
            if selected_entity_references := ui_state.entityReferences():
                entity_ref_str = selected_entity_references[0].toString()
                container.setResult(entity_ref_str)
            else:
                container.setResult(None)

        ui_request = OpenKatanaAssetIOUIRequest(
            native_data=container,
            entity_references=entity_refs,
            entity_traits_datas=[entity_traits],
            state_changed_callback=state_changed_cb,
        )

        initial_state = self.__ui_service.ui_delegate().populateUI(
            ui_traits, ui_access, ui_request, self.__ui_service.context()
        )
        if initial_state is None:
            return

        initial_state_entity_refs = initial_state.entityReferences()
        if initial_state_entity_refs:
            container.setResult(initial_state_entity_refs[0].toString())

        def widget_factory(parent):
            container.setParent(parent)
            return container

        tab_idx = browser.addBrowserTab(
            widget_factory, DisplayNameTrait(ui_policy).getName("Asset")
        )
        browser.setCurrentIndex(tab_idx)

    @exception_logger_decorator
    def createAssetRenderWidget(self, parent, outputInfo):
        w = OpenKatanaAssetIOAssetRenderWidget(parent, self.getWidgetHints(), outputInfo)
        parent.layout().addWidget(w)
        return w


class SimpleTextBoxBrowser(QtWidgets.QFrame):
    """
    An asset "browser", which is actually just a single text box.

    This is useful to allow asset IDs to be typed/pasted when browsing
    for assets, since asset IDs will be rejected by the default file
    browser, leaving no way to enter an asset ID in certain Katana
    inputs.

    Ideally the OpenAssetIO plugin for the asset management system will
    supply an OpenAssetIO UI Delegate plugin too, so that this won't be
    used.
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


class OpenKatanaAssetIOAssetRenderWidget(
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
            render_node_editor_widget.getValuePolicy().getNode().getParameter(f"outputs.preflight")
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


class OpenKatanaAssetIOControlWidget(UI4.Util.AssetWidgetDelegatePlugins.BaseAssetControlWidget):
    """
    Delegated widget for displaying asset IDs.

    I.e. replacement for the default text boxes showing asset ID/file
    path in the Parameters tab etc.
    """

    def __init__(self, ui_service: UIDelegateService, ui_traits, ui_access, *args, **kwargs):
        self.__ui_traits = ui_traits
        self.__ui_access = ui_access
        self.__entity_str = ""
        self.__ui_request = None
        self.__update_request_cb = None
        self.__ui_service = ui_service
        super().__init__(*args, **kwargs)

    def buildWidgets(self, hints):
        if hints.get("context") == "katana scene context":
            spec = WorkfileSpecification.create()
            spec.locatableContentTrait().setMimeType("application/vnd.foundry.katana.project")
            entity_traits = spec.traitsData()
            katana_openassetio.traits.application.ProjectTrait.imbueTo(entity_traits)
        elif hints.get("context") == "look file context":
            spec = WorkfileSpecification.create()
            spec.locatableContentTrait().setMimeType("application/vnd.foundry.katana.lookfile")
            entity_traits = spec.traitsData()
            katana_openassetio.traits.application.LookFileTrait.imbueTo(entity_traits)
        else:
            entity_traits = TraitsData()
            LocatableContentTrait.imbueTo(entity_traits)

        def state_changed_cb(ui_state):
            prev_entity_str = self.__entity_str
            ui_state_entity_refs = ui_state.entityReferences()
            if ui_state_entity_refs:
                self.__entity_str = ui_state_entity_refs[0].toString()
            else:
                self.__entity_str = ""
            if self.__entity_str != prev_entity_str:
                self.emitValueChanged()

        self.__ui_request = OpenKatanaAssetIOUIRequest(
            entity_traits_datas=[entity_traits],
            state_changed_callback=state_changed_cb,
        )

        initial_state = self.__ui_service.ui_delegate().populateUI(
            self.__ui_traits, self.__ui_access, self.__ui_request, self.__ui_service.context()
        )

        if initial_state_entity_refs := initial_state.entityReferences():
            self.__entity_str = initial_state_entity_refs[0].toString()

        self.__update_request_cb = initial_state.updateRequestCallback()

        self.__widget = initial_state.nativeData()
        self.layout().addWidget(self.__widget)

    def setValue(self, value):
        if self.__update_request_cb is None:
            return
        if self.__entity_str == value:
            return
        if entity_ref := self.__ui_service.manager().createEntityReferenceIfValid(value):
            self.__ui_request.setEntityReferences([entity_ref])
        self.__update_request_cb(self.__ui_request)

    def setPalette(self, palette):
        self.__widget.setPalette(palette)

    def setReadOnly(self, readOnly):
        # TODO(DF): Support this.
        # TODO(DF): Do we need an ability to update UI traits after
        #  construction?
        pass

    def getValue(self):
        return self.__entity_str


class OpenKatanaAssetIOBrowserFrame(QtWidgets.QFrame):

    selectionValidSignal = QtCore.Signal(bool)

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.__result = None
        self.setLayout(QtWidgets.QVBoxLayout())

    def setResult(self, entity_reference_str):
        self.__result = entity_reference_str
        self.selectionValidSignal.emit(self.selectionValid())

    def getResult(self):
        return self.__result

    def selectionValid(self):
        """
        Required method for asset browsers.

        @rtype: C{bool}
        @return: C{True} if the currently selected path is a valid
        selection, otherwise C{False}.
        """
        return self.__result is not None


class OpenKatanaAssetIOUIRequest(UIDelegateRequestInterface):
    """
    OpenAssetIO UI delegate request implementation.
    """

    def __init__(
        self,
        native_data=None,
        entity_references=None,
        entity_traits_datas=None,
        state_changed_callback=None,
    ):
        UIDelegateRequestInterface.__init__(self)
        self.__native_data = native_data
        self.__entity_references = entity_references or []
        self.__entity_traits_datas = entity_traits_datas or []
        self.__state_changed_callback = state_changed_callback

    def nativeData(self):
        return self.__native_data

    def entityReferences(self):
        return self.__entity_references

    def entityTraitsDatas(self):
        return self.__entity_traits_datas

    def stateChangedCallback(self):
        return self.__state_changed_callback

    def setEntityReferences(self, entityReferences):
        self.__entity_references = entityReferences


PluginRegistry = [
    ("AssetWidgetDelegate", 1, "KatanaOpenAssetIO", OpenKatanaAssetIOWidgetDelegate),
]
