# RealityStream Plugin

A Unreal Engine 5.6 plugin for real-time segmentation, 3D reconstruction, and point cloud visualization. The plugin integrates with ComfyUI for live texture streaming and provides tools for importing and visualizing 3D reconstruction data.

## Features

### ComfyStream - Real-time Texture Streaming
- **WebSocket Connection**: Connect to ComfyUI WebViewer server via WebSocket
- **Triple Texture Stream**: Receives RGB, Depth, and Mask textures in real-time
- **Dynamic Material Updates**: Automatically updates material parameters when textures arrive
- **Depth-based 3D Reconstruction**: Calculates world positions from depth maps
- **Dynamic Actor Spawning**: Spawns actors at calculated 3D positions based on depth
- **Texture Lerping**: Optional smooth interpolation between texture updates
- **Auto-reconnection**: Automatic reconnection on connection loss
- **Blueprint Support**: Full Blueprint integration with events and functions

### SplatCreator - Point Cloud Visualization
- **PLY File Import**: Loads and visualizes PLY point cloud files
- **Adaptive Sphere Rendering**: Uses Hierarchical Instanced Static Mesh with adaptive sphere sizes
- **Smooth Morphing**: Smooth interpolation between different point clouds
- **Automatic Cycling**: Automatically cycles through PLY files in the SplatCreatorData folder
- **Point Filtering**: Uniform sampling to optimize performance

### Hyper3DObjects - Mesh Import System
- **OBJ Import**: Imports OBJ meshes with textures from the MeshImport folder
- **Procedural Mesh Generation**: Creates procedural meshes from OBJ files
- **Texture Support**: Supports diffuse, metallic, normal, roughness, PBR, and shaded textures
- **Floating Animation**: Objects float and bob in 3D space
- **Automatic Discovery**: Scans and imports all OBJ files in the MeshImport directory

## Installation

Premade blueprints and materials are included in the Blueprints and Materials folder. 

1. Copy the `RealityStream` folder to your project's `Plugins` directory
2. Regenerate project files (Right-click `.uproject` → Generate Visual Studio project files)
3. Compile your project in Unreal Engine 5.6

## Usage

### ComfyStream Setup

#### Basic Actor Setup

1. Add a `ComfyStreamActor` to your level
2. Set the `Base Material` property to a material with these texture parameters:
   - You can use the M_displacement asset 
   - `RGB_Map` (Texture2D Parameter)
   - `Depth_Map` (Texture2D Parameter)
   - `Mask_Map` (Texture2D Parameter)
   - Optional: `Opacity` (Scalar Parameter) for fade effects
   - Optional: `LerpAlpha` (Scalar Parameter) for texture lerping
3. Configure the `Segmentation Channel Config`:
   - **Server URL**: ComfyUI WebViewer WebSocket URL (default: `ws://localhost:8001`)
   - **Channel Number**: WebSocket channel number (default: 1)
   - **Auto Reconnect**: Enable automatic reconnection
4. The actor will automatically connect and start receiving textures

#### Blueprint Events

- `OnTextureReceived(UTexture2D*)`: Called when a new texture is received
- `OnConnectionStatusChanged(bool)`: Called when connection status changes
- `OnError(FString)`: Called when an error occurs

#### C++ Usage

```cpp
// Get the ComfyStreamActor
AComfyStreamActor* StreamActor = GetWorld()->SpawnActor<AComfyStreamActor>();

// Configure the material
StreamActor->BaseMaterial = YourMaterial;

// Configure connection
StreamActor->SegmentationChannelConfig.ServerURL = TEXT("ws://192.168.1.65:8001");
StreamActor->SegmentationChannelConfig.ChannelNumber = 1;
StreamActor->SegmentationChannelConfig.bAutoReconnect = true;

// Connect manually (or enable Auto Reconnect)
StreamActor->ConnectSegmentationChannel();
```

### SplatCreator Setup

1. Place PLY files in: `Plugins/RealityStream/SplatCreatorOutputs/`
2. In Blueprint or C++, call:
   ```
   Get Splat Creator Subsystem -> Start Point Cloud System
   ```

3. Place the M_VertexColor material in your project for the color.
   
4. The system will automatically:
   - Scan for PLY files
   - Load and display the first point cloud
   - Cycle through files automatically
   - Morph smoothly between different point clouds

#### PLY File Format

The plugin expects PLY files with:
- Vertex positions (X, Y, Z)
- Vertex colors (R, G, B)
- ASCII or binary format supported

### Hyper3DObjects Setup

1. Place OBJ files and textures in: `Plugins/RealityStream/MeshImport/[ObjectName]/`
2. Supported texture files:
   - `texture_diffuse.png`
   - `texture_metallic.png`
   - `texture_normal.png`
   - `texture_roughness.png`
   - `texture_pbr.png`
   - `shaded.png`
3. In Blueprint or C++, call:
   ```
   Get Hyper 3D Objects Subsystem -> Activate Object Imports
   ```

4. Place the M_ProceduralMeshTexture material in your project for the color.
   
5. Objects will be imported and animated automatically

## Configuration


### ComfyStream Configuration

```cpp
FComfyStreamConfig Config;
Config.ServerURL = TEXT("ws://localhost:8001");
Config.ChannelNumber = 1;
Config.ChannelType = EComfyChannel::Segmentation;
Config.bAutoReconnect = true;
Config.ReconnectDelay = 5.0f;
Config.bEnableLerpSmoothing = false;
Config.LerpSpeed = 5.0f;
Config.LerpThreshold = 0.01f;
```

### ComfyStreamActor Settings

- **Actor Lifetime Seconds**: How long spawned actors persist (default: 3.0s)
- **Lerp Speed**: Texture transition speed (default: 2.0)
- **Location Threshold**: Distance threshold for reusing actors (default: 50.0)
- **Fade Out Duration**: Fade-out duration before actor destruction (default: 0.5s)

### Depth Reconstruction Settings

Configured on `ComfyStreamComponent`:
- **Focal Scale**: Camera focal length multiplier (default: 1.2)
- **Depth Scale Units**: Maximum depth distance in world units (default: 500.0)

## API Reference

### ComfyStreamActor

Main actor for receiving and displaying ComfyUI streams.

**Properties:**
- `BaseMaterial` - Material with RGB_Map, Depth_Map, Mask_Map parameters
- `SegmentationChannelConfig` - WebSocket connection configuration
- `ActorLifetimeSeconds` - Lifetime of spawned texture actors
- `LerpSpeed` - Texture transition speed
- `LocationThreshold` - Distance threshold for actor reuse
- `FadeOutDuration` - Fade-out duration

**Functions:**
- `ConnectSegmentationChannel()` - Connect to ComfyUI
- `DisconnectAll()` - Disconnect from all channels

**Events:**
- `OnTextureReceived(UTexture2D*)` - Texture received
- `OnConnectionStatusChanged(bool)` - Connection status changed
- `OnError(FString)` - Error occurred

### ComfyStreamComponent

Component for handling individual ComfyUI streams.

**Properties:**
- `StreamConfig` - Connection configuration
- `FocalScale` - Camera focal length scale
- `DepthScaleUnits` - Maximum depth distance

**Functions:**
- `Connect()` - Connect to server
- `Disconnect()` - Disconnect from server
- `IsConnected()` - Check connection status
- `GetConnectionStatus()` - Get current connection status

### ComfyImageFetcher

Internal class for WebSocket communication and PNG decoding.

**Features:**
- WebSocket message handling
- PNG stream splitting (handles concatenated PNGs)
- RGB/Depth/Mask identification by color type and size
- Automatic frame accumulation

### SplatCreatorSubsystem

Game instance subsystem for point cloud visualization.

**Functions:**
- `StartPointCloudSystem()` - Initialize and start the point cloud system

**Features:**
- Automatic PLY file scanning
- Point cloud morphing
- Adaptive sphere sizing based on point density
- Uniform sampling for performance

### Hyper3DObjectsSubsystem

Game instance subsystem for OBJ mesh import.

**Functions:**
- `ActivateObjectImports()` - Start importing and animating objects
- `DeactivateObjectImports()` - Stop importing objects

**Features:**
- Automatic OBJ discovery
- Texture loading and application
- Procedural mesh generation
- Floating/bobbing animation

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
│       ├── SplatCreator/        # Public headers
│       └── MeshImport/           # Public headers
├── SplatCreatorData/            # Place PLY files here
├── MeshImport/                  # Place OBJ files and textures here
└── README.md
```

## Dependencies

- **WebSockets** - For ComfyUI WebSocket communication
- **ImageWrapper** - For PNG/JPEG decoding
- **ProceduralMeshComponent** - For mesh generation
- **Json** / **JsonUtilities** - For JSON parsing
- **RenderCore** / **RHI** - For texture operations

## Troubleshooting

### ComfyStream Issues

1. **No textures received**: 
   - Verify ComfyUI WebViewer is running and accessible
   - Check WebSocket URL and channel number
   - Ensure ComfyUI is sending RGB, Depth, and Mask textures

2. **Material not updating**:
   - Verify material has exact parameter names: `RGB_Map`, `Depth_Map`, `Mask_Map`
   - Check that BaseMaterial is assigned to ComfyStreamActor

3. **Actors not spawning**:
   - Verify depth texture is being received
   - Check DepthScaleUnits and FocalScale settings
   - Ensure DisplayMesh has a static mesh assigned

### SplatCreator Issues

1. **No point clouds visible**:
   - Verify PLY files are in `SplatCreatorOutputs/` folder
   - Check that `StartPointCloudSystem()` was called
   - Verify PLY files contain vertex positions and colors

2. **Performance issues**:
   - Point clouds are automatically sampled to 100,000 points max to prevent unnecessary culling. 
   - Adjust sphere sizes in code if needed

### Hyper3DObjects Issues

1. **Objects not importing**:
   - Verify OBJ files are in `MeshImport/[ObjectName]/` folders
   - Check that `ActivateObjectImports()` was called
   - Ensure OBJ files are valid

2. **Textures not loading**:
   - Verify texture files are in the same folder as the OBJ
   - Check texture file names match expected names
   - Ensure textures are valid image files

## Technical Details

### Texture Processing

- Textures are received as PNG streams over WebSocket
- PNGs are automatically identified as RGB, Depth, or Mask based on:
  - Color type detection (RGB vs grayscale)
  - File size comparison
- Textures are accumulated until a complete frame (RGB + Depth + Mask) is received

### Depth Reconstruction

- Uses DepthAnything depth format (normalized 0-1, where 1.0 = near, 0.0 = far)
- Automatically estimates camera intrinsics from texture dimensions
- Converts depth to world coordinates using Unreal's coordinate system (Z forward, X right, Y up)

### Point Cloud Rendering

- Uses Hierarchical Instanced Static Mesh (HISM) for efficient rendering
- Adaptive sphere sizes based on nearest neighbor distance
- High density areas use smaller spheres (0.08 units)
- Sparse areas use larger spheres (0.3 units)

## License

This plugin is provided as-is for educational and development purposes.

## Author

Created by Alexia Hartogensis  
Website: https://alexiahartogensis.com
