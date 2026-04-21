# RealityStream Plugin

A Unreal Engine 5.6 plugin for interpreting AI outputs from 3D and 2D generations to Gaussian Splats. 

## Installation

### Adding to Your Unreal Engine Project

You can add RealityStream to your Unreal Engine project in two ways:

Make sure to create an Unreal Engine project in C++ NOT Blueprints.

#### Option 1: Git Clone
1. Navigate to your project's `Plugins` directory or create the directory.
2. Clone the repository:
   ```bash
   git clone [repository-url]
   ```
3. Rename the folder to RealityStream.
4. Add the plugin to your `.uproject` file under the `Plugins` section:
   ```json
   "Plugins": [
     {
       "Name": "RealityStream",
       "Enabled": true
     }
   ]
   ```

#### Option 2: Download ZIP
1. Download the RealityStream plugin as a ZIP file
2. Extract the contents and place it in the directory `Plugins` or create one. 
3. Rename the file to RealityStream
4. Add the plugin to your `.uproject` file under the `Plugins` section:
   ```json
   "Plugins": [
     {
       "Name": "RealityStream",
       "Enabled": true
     }
   ]
   ```

#### Finalizing Installation
1. Right-click your `.uproject` file and select **Generate Visual Studio project files**
2. Open the project in Unreal Engine 5.6
3. The plugin will compile automatically on first launch

## Basic Setup

### Required Materials

The plugin includes example materials in the `Blueprints` and `Materials` folder. You'll need to set up the following materials for full functionality:

#### M_Displacement (for ComfyStreamActor)
- **Blend Mode**: Masked
- **Shading Model**: Default Lit (Surface)
- **Required Parameters**:
  - `RGB_Map` (Texture2D Parameter)
  - `Mask_Map` (Texture2D Parameter)
  - `Depth_Map_Object` (Texture Object Parameter) - Connect to `MF_DepthToNormal` material function to convert depth to normal map

#### M_ProceduralMeshTexture (for Hyper3DObjects)
- **Blend Mode**: Masked
- **Shading Model**: Default Lit (Surface)
- **Required Parameters**:
  - `Diffuse` (Texture2D Parameter)
  - `Metallic` (Texture2D Parameter)
  - `Roughness` (Texture2D Parameter)
  - `Normal` (Texture2D Parameter)
  - `Opacity` (Scalar Parameter)

#### M_SplatMorph (for SplatCreator)
- **Blend Mode**: Translucent
- **Shading Model**: Default Lit (Surface)
- **Contact Shadows**: Enabled
- **Two Sided**: Enabled
- **Required Parameters**:
  - `MorphProgress` (Scalar Parameter) - Controls morph transition (0 to 1)
- **Base Color Setup**:
  - Use `PerInstanceCustomData` nodes with indices 0, 1, 2 to get RGB color from splat vertex data
- **World Position Offset Setup**:
  - Use `PerInstanceCustomData` index 4 for Y transition
  - Use `PerInstanceCustomData` index 5 for Z transition
  - Multiply by `(1 - MorphProgress)` to start at smallest and grow to full size

### Getting Started

Once you've set up the materials, use the provided Blueprint instances to get results similar to the RealityStream installation examples.

## Blueprint Systems

### Hyper3DObjectsSubsystem

The **Hyper3DObjectsSubsystem** is a Game Instance Subsystem that handles 3D object import and placement. If you don't want 3D objects in your scene, you can skip this subsystem.

#### Blueprint Functions

**Placement Configuration:**
- `Set Object Scale` / `Get Object Scale` - Set the scale of imported objects (default: 50)
- `Set Placement Box Size` - Set the random placement area size (0 = objects stay in place, larger values = objects randomly placed within that box size)
- `Update from Splat Dimensions` - Place objects inside the splat dimensions instead of using the placement box
- `Set Drive Placement Box` - Boolean to determine if objects follow the placement box or follow the splat

**Object Management:**
- `Activate Object Imports` - Activate and start importing objects
- `Deactivate Object Imports` - Deactivate and stop importing objects
- `Set Total Instances` - Set how many floating objects you want in the scene

**3D Object Fade Control:**
- `Get Hyper3D Object Fade Enabled` / `Set Hyper3D Object Fade Enabled` - Enable/disable opacity fading for 3D objects
- `Get Hyper3D Object Fade In Duration` / `Set Hyper3D Object Fade In Duration` - Control fade in duration (0.01-60 seconds, default: 2 seconds)
- `Get Hyper3D Object Hold Duration` / `Set Hyper3D Object Hold Duration` - Control how long objects stay at full opacity (0 = stay visible forever, default: 10 seconds)

**ComfyUI Exclusion Zone:**
- `Set Comfy Stream Exclusion Zone` - Set an exclusion zone so 3D objects don't overlap with streamed objects
- `Get Reference Location` / `Set Reference Location` - Get or set the exclusion zone reference location
  - Example: Set based on actor location to avoid overlap

#### Setup Example

1. Place 3D mesh files in `Plugins/RealityStream/MeshImport/` 
2. Access the subsystem in Blueprint: `Get Hyper 3D Objects Subsystem`
3. Configure placement settings (scale, box size, total instances)
4. Set up exclusion zones if using ComfyStream
5. Call `Activate Object Imports` to start

### SplatCreatorSubsystem

The **SplatCreatorSubsystem** is a Game Instance Subsystem that handles point cloud visualization and cycling.

#### Blueprint Functions

**System Control:**
- `Start Point Cloud System` - Initialize and start the point cloud visualization system

**Splat Image Sending:**
- `Set Send Current or Next` - Determine whether to send the current splat image or the next splat image to ComfyUI
- The system can send a PNG with the same name as the splat to ComfyUI, allowing your ComfyUI output to match the splat appearance

**ComfyUI Image Preview:**
- `Set Image Preview Target` - Configure where the ComfyUI preview image appears
  - **Target Plane**: The plane/mesh to display the preview on
  - **Material**: The material instance to use
  - **Target Name**: Unique identifier for this preview (each preview MUST have a separate target name)
- If you want the preview image to appear, you must set up an image preview target

**Cycle Control:**
- `Start Next Cycle` - Manually trigger the next cycle/splat
- `Get Cycle Length` - Returns the current cycle interval in seconds
- `Set Cycle Length` - Change how often splats automatically cycle (accepts 1-300 seconds, default: 16 seconds)
- By default, the system automatically changes cycles every 16 seconds

**Preview Image Fade Control:**
- `Get Preview Image Fade Enabled` / `Set Preview Image Fade Enabled` - Enable/disable opacity fading for preview images
- `Get Preview Image Fade In Duration` / `Set Preview Image Fade In Duration` - Control fade in duration (0-10 seconds, default: 1 second)
- `Get Preview Image Hold Duration` / `Set Preview Image Hold Duration` - Control how long preview stays at full opacity (0-60 seconds, default: 4 seconds)
- `Get Preview Image Fade Out Duration` / `Set Preview Image Fade Out Duration` - Control fade out duration (0.1-60 seconds, default: 4 seconds)

#### Setup Example

1. Place PLY files in `Plugins/RealityStream/SplatCreatorOutputs/`
2. Access the subsystem in Blueprint: `Get Splat Creator Subsystem`
3. Call `Start Point Cloud System`
4. (Optional) Configure image preview target for ComfyUI output
5. (Optional) Call `Start Next Cycle` to manually control cycling

### ComfyStreamActor

The **ComfyStreamActor** allows you to send any PNG from ComfyUI and automatically import it into Unreal Engine via WebSocket.

#### Setup Steps

1. **Create Blueprint**: Create a Blueprint of type `ComfyStreamActor`
2. **Set Base Material**: Add a base material (see M_Displacement requirements above)
3. **Configure Segmentation Channel**:
   - **Server URL**: WebSocket URL for ComfyUI connection
     - If running ComfyUI on the same computer, use: `ws://localhost:8001`
   - **Channel Number**: The WebSocket channel number
   - **Channel Type**: Segmentation (only option currently)
   - **Ping Interval**: Keep-alive ping interval in seconds
   - **Auto Reconnect**: Enable to automatically reconnect after disconnecting
   - **Reconnect Delay**: Time in seconds to wait before attempting reconnection
   - **Enable Lerp Smoothing**: Smooth interpolation between frames
   - **Lerp Speed**: Speed of interpolation in seconds
   - **Lerp Threshold**: Threshold to consider lerp complete
   - **Frame Apply Delay**: Seconds to wait before applying the next frame

#### ComfyUI Workflow

An example ComfyUI workflow is provided. Any workflow that outputs a PNG through WebSockets will work with this system.

## Required Materials Reference

### For ComfyStreamActor: M_Displacement

**Material Setup:**
- **Blend Mode**: Masked
- **Shading Model**: Default Lit (Surface)

**Required Parameters:**
- `RGB_Map` (Texture2D Parameter)
- `Mask_Map` (Texture2D Parameter)
- `Depth_Map_Object` (Texture Object Parameter)
  - Connect to `MF_DepthToNormal` material function to convert depth to normal

### For Hyper3DObjects: M_ProceduralMeshTexture

**Material Setup:**
- **Blend Mode**: Masked (recommended for fading effect)
- **Shading Model**: Default Lit (Surface)

**Required Parameters:**
- `Diffuse` (Texture2D Parameter)
- `Metallic` (Texture2D Parameter)
- `Roughness` (Texture2D Parameter)
- `Normal` (Texture2D Parameter)
- `Opacity` (Scalar Parameter)

### For SplatCreator: M_SplatMorph

**Material Setup:**
- **Blend Mode**: Translucent
- **Shading Model**: Default Lit (Surface)
- **Contact Shadows**: Enabled
- **Two Sided**: Enabled

**Base Color Setup:**
Use `PerInstanceCustomData` nodes to extract vertex color from splat:
- Index 0: Red channel
- Index 1: Green channel
- Index 2: Blue channel

**World Position Offset Setup:**
For morphing transition effect:
- `PerInstanceCustomData` Index 4: Y transition
- `PerInstanceCustomData` Index 5: Z transition
- Use `(1 - MorphProgress)` to create smallest-to-largest transition

**Required Parameters:**
- `MorphProgress` (Scalar Parameter) - Controls transition animation (0 = start, 1 = complete)

## Material Location & Asset Paths

The plugin automatically searches for materials in multiple locations with a smart fallback system. You don't need to place materials in a specific spot, but following the recommended paths ensures optimal performance.

### Recommended Location (Primary Search Path)
Place your materials in: **`/Game/_GENERATED/Materials/`**

The plugin prioritizes this location for:
- `M_ProceduralMeshTexture`
- `M_VertexColor`
- `M_SplatMorph`
- `M_image`

### Fallback Search Paths

If materials aren't found in the primary location, the plugin automatically searches:

1. **Specific Fallback Paths:**
   - `/Game/M_ProceduralMeshTexture.M_ProceduralMeshTexture`
   - `/Game/ImportedTextures/M_ProceduralMeshTexture.M_ProceduralMeshTexture`
   - `/Game/M_VertexColor.M_VertexColor`

2. **Asset Registry Search:**
   - The plugin uses Unreal's Asset Registry to search the entire `/Game/` directory for materials with matching names

3. **Engine Defaults (Last Resort):**
   - `/Engine/EngineMaterials/DefaultMaterial`
   - `/Engine/BasicShapes/BasicShapeMaterial`
   - `/Engine/EditorMaterials/WidgetVertexColorMaterial`

### Summary
While you can place materials anywhere in your `/Game/` content folder and the plugin will find them, using `/Game/_GENERATED/Materials/` is recommended for best performance and to avoid unnecessary asset registry searches.

## File Structure

```
RealityStream/
├── Source/RealityStream/
│   ├── Private/
│   │   ├── ComfyStream/        # ComfyUI streaming implementation
│   │   ├── SplatCreator/        # Point cloud visualization
│   │   └── MeshImport/          # OBJ import system
│   └── Public/
│       ├── ComfyStream/          # Public headers
│       ├── SplatCreator/         # Public headers
│       └── MeshImport/           # Public headers
├── SplatCreatorOutputs/         # Place PLY files here
├── MeshImport/                  # Place OBJ files and textures here
├── Blueprints/                  # Example blueprints
├── Materials/                   # Example materials
└── README.md
```

## License

This plugin is provided as-is for educational and development purposes.

## Author

Created by Alexia Hartogensis  
Website: https://alexiahartogensis.com
