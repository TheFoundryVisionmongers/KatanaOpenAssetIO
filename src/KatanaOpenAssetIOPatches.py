# KatanaOpenAssetIO
# Copyright (c) 2025 The Foundry Visionmongers Ltd
# SPDX-License-Identifier: Apache-2.0
"""
Katana plugin to decorate 'Pre'/'Post-Render Publish Asset' such that
the working reference returned from `createAssetAndPath()` is not
discarded, but is instead used for resolving the destination path for
renders.
"""
import Nodes3DAPI
import PyFnAttribute

OriginalPreCreateProductAndLocation = Nodes3DAPI.RenderNodeUtil.PreCreateProductAndLocation
OriginalPostCreateProductAndLocation = Nodes3DAPI.RenderNodeUtil.PostCreateProductAndLocation
OriginalRenderNodeInfo = Nodes3DAPI.RenderNodeUtil.RenderNodeInfo


def PreCreateProductAndLocation(node, idx, versionup, assetTxn=None):
    """
    Decorate original implementation to add a hidden parameter to the
    Render node that tracks the "preflight" reference returned from
    `createAssetAndPath()`.

    This function is responsible for calling `createAssetAndPath()` in
    the AssetAPI plugin, signalling to the manager that a render is
    about to be performed, allowing the manager to prepare (e.g. create
    destination paths, placeholder database entries, etc).

    When the user selects 'Pre-Render Publish Asset' in the Render node
    UI, this function is called, but its return value is discarded,
    including the working reference that should be used for subsequent
    `resolveAsset()` calls to get the destination path for renders.

    This decorator persists the returned assetId as a parameter on the
    node, to be retrieved by the `RenderNodeInfo` class, decorated
    below.
    """
    global OriginalPreCreateProductAndLocation
    output_info = OriginalPreCreateProductAndLocation(node, idx, versionup, assetTxn=assetTxn)

    if output_info is None:
        # Output is not an asset ID.
        return None

    output_name = output_info["outputName"]
    preflight_asset_id = output_info["assetId"]

    outputs_param = node.getParameter("outputs")
    preflight_param = outputs_param.getChild("preflight")
    if not preflight_param:
        preflight_param = outputs_param.createChildGroup("preflight")

    if param := preflight_param.getChild(output_name):
        param.setValue(preflight_asset_id, 0)
    else:
        preflight_param.createChildString(output_name, preflight_asset_id)

    return output_info


def PostCreateProductAndLocation(node, idx, versionup, assetTxn=None):
    """
    Decorate original implementation to remove the hidden parameter on
    the Render node that was added by the decorated
    PreCreateProductAndLocation (above).

    This function is called when the user selects 'Post-Render Publish
    Asset' in the Render node UI.

    This function is responsible for calling `postCreateAsset()` in the
    AssetAPI plugin, signalling to the manager that the render is
    complete and its output is ready to be consumed.
    """
    global OriginalPostCreateProductAndLocation
    output_info = OriginalPostCreateProductAndLocation(node, idx, versionup, assetTxn=assetTxn)

    if output_info is None:
        # Output is not an asset ID.
        return None

    if preflight_param := node.getParameter("outputs.preflight"):
        output_name = output_info["outputName"]
        if output_param := preflight_param.getChild(output_name):
            preflight_param.deleteChild(output_param)

    return output_info


class RenderNodeInfo(OriginalRenderNodeInfo):
    """
    Decorate the `RenderNodeInfo` class to resolve the destination path
    from the assetId stored in the new hidden parameter on the Render
    node, added by the decorated `PreCreateProductAndLocation()`
    (above).

    This `RenderNodeInfo` class is used by Katana's rendering system to
    gather all the information needed to perform a render, including the
    destination path.

    Normally, the "renderLocation" in the render settings comes directly
    from an upstream RenderOutputDefine node (when its "locationType" is
    set to "file").

    Katana assumes that this "renderLocation" asset ID is the target
    destination of the render. However, this target asset ID could be
    e.g. a previous version or a container, which is not meant to be
    overwritten, but rather provides an anchor or starting point.

    Instead, Katana should pass the "renderLocation" asset ID to
    `createAssetAndPath()`, which returns an asset ID for subsequent
    queries related to publishing.

    In OpenAssetIO terminology the return value of
    `createAssetAndPath()` is a "working" entity reference. It is meant
    to be used for resolving metadata specifically related to the
    publish, in this case the destination file path.

    This working reference is stored in a Render node parameter by the
    decorated `PreCreateProductAndLocation()` function (above), which is
    triggered manually by the user clicking 'Pre-Render Publish Asset'.

    We need to ensure this working reference is used when retrieving the
    destination file path from the asset system.

    We decorate this class so that the "renderLocation" setting from the
    upstream RenderOutputDefine is overridden to instead be the working
    reference, if available.
    """

    def getOutputInfoByIndex(self, index, forceLocal, makeVersionTemplate=False):
        name, attrs, _scope = self.__outputList[index]

        output_info = self.__get_output_info_using_preflight_asset_id(
            name,
            attrs,
            lambda: OriginalRenderNodeInfo.getOutputInfoByIndex(
                self, index, forceLocal, makeVersionTemplate=makeVersionTemplate
            ),
        )
        return output_info

    def getOutputInfoByName(self, name, forceLocal, makeVersionTemplate=False):
        attrs = self.__find_attrs_in_output_list(name)
        if attrs is None:
            # Will raise KeyError.
            return OriginalRenderNodeInfo.getOutputInfoByName(
                self, name, forceLocal, makeVersionTemplate=makeVersionTemplate
            )

        output_info = self.__get_output_info_using_preflight_asset_id(
            name,
            attrs,
            lambda: OriginalRenderNodeInfo.getOutputInfoByName(
                self, name, forceLocal, makeVersionTemplate=makeVersionTemplate
            ),
        )
        return output_info

    def __find_attrs_in_output_list(self, name):
        for candidate_name, attrs, _scope in self.__outputList:
            if candidate_name == name:
                return attrs
        return None

    def __get_output_info_using_preflight_asset_id(self, name, attrs, get_output_info):
        preflight_asset_id_param = self.__node.getParameter(f"outputs.preflight.{name}")

        if preflight_asset_id_param is None:
            # User has not clicked 'Pre-Render Publish Asset' (yet).
            return get_output_info()

        # Get the value of the working reference.
        preflight_asset_id = preflight_asset_id_param.getValue(0)

        # Get the location settings to mutate.
        original_location_settings = attrs["locationSettings"]

        # Update the location settings with the asset ID that should
        # be resolved to give the destination file path.
        gb = PyFnAttribute.GroupBuilder()
        gb.update(original_location_settings)
        gb.set("renderLocation", PyFnAttribute.StringAttribute(preflight_asset_id))
        patched_location_settings = gb.build()

        # Mutate the attrs dictionary for this output, which will be
        # read by getOutputInfoBy*() functions.
        attrs["locationSettings"] = patched_location_settings

        # Execute the original getOutputInfoBy*() function, which
        # will use the patched settings to determine the destination
        # file path.
        output_info = get_output_info()

        return output_info


Nodes3DAPI.RenderNodeUtil.PostCreateProductAndLocation = PostCreateProductAndLocation
Nodes3DAPI.RenderNodeUtil.PreCreateProductAndLocation = PreCreateProductAndLocation
Nodes3DAPI.RenderNodeUtil.RenderNodeInfo = RenderNodeInfo
