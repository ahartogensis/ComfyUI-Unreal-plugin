#!/usr/bin/env python3
"""
Gaussian Splatting PLY → OBJ (with Unreal Engine coordinates) + optional volumetric remeshing.
Recreates Blender Geometry Nodes "Points to Volume → Volume to Mesh" workflow in pure Python.

Requires: numpy, scipy, open3d
"""

import sys, os, struct, math
import numpy as np
from scipy.spatial import cKDTree
import open3d as o3d


# ============================================================
# === PLY Reading ============================================
# ============================================================

def read_ply_header(file_path):
    """Read PLY header and return vertex count and properties."""
    with open(file_path, 'rb') as f:
        header_lines = []
        while True:
            line_bytes = f.readline()
            try:
                line = line_bytes.decode('utf-8').strip()
            except UnicodeDecodeError:
                if b'end_header' in line_bytes:
                    line = 'end_header'
                else:
                    continue
            header_lines.append(line)
            if line == 'end_header':
                break
        
        vertex_count = 0
        properties = []
        for line in header_lines:
            if line.startswith('element vertex'):
                vertex_count = int(line.split()[-1])
            elif line.startswith('property'):
                parts = line.split()
                if len(parts) >= 3:
                    prop_type = parts[1]  # float, double, uchar, etc.
                    prop_name = parts[2]
                    properties.append((prop_name, prop_type))
        
        return vertex_count, properties


def read_ply_data(file_path, vertex_count, properties):
    """Read PLY binary data."""
    positions, normals, colors = [], [], []
    with open(file_path, 'rb') as f:
        # Skip header
        while True:
            line_bytes = f.readline()
            if b'end_header' in line_bytes:
                break
        
        # Build format string and calculate byte size
        format_str = ''
        prop_names = []
        for prop_name, prop_type in properties:
            prop_names.append(prop_name)
            if prop_type == 'float':
                format_str += 'f'
            elif prop_type == 'double':
                format_str += 'd'
            elif prop_type == 'uchar':
                format_str += 'B'
            elif prop_type == 'int':
                format_str += 'i'
        
        vertex_size = struct.calcsize(format_str)
        
        # Read vertex data
        for _ in range(vertex_count):
            vertex_data = struct.unpack(format_str, f.read(vertex_size))
            
            # Extract positions (x, y, z)
            if 'x' in prop_names and 'y' in prop_names and 'z' in prop_names:
                x_idx = prop_names.index('x')
                y_idx = prop_names.index('y')
                z_idx = prop_names.index('z')
                positions.append([vertex_data[x_idx], vertex_data[y_idx], vertex_data[z_idx]])

            # Extract normals
            if 'nx' in prop_names and 'ny' in prop_names and 'nz' in prop_names:
                nx_idx = prop_names.index('nx')
                ny_idx = prop_names.index('ny')
                nz_idx = prop_names.index('nz')
                normals.append([vertex_data[nx_idx], vertex_data[ny_idx], vertex_data[nz_idx]])

            # Extract colors from RGB (uchar 0-255)
            if 'red' in prop_names and 'green' in prop_names and 'blue' in prop_names:
                r_idx = prop_names.index('red')
                g_idx = prop_names.index('green')
                b_idx = prop_names.index('blue')
                rgb = [vertex_data[r_idx]/255.0, vertex_data[g_idx]/255.0, vertex_data[b_idx]/255.0]
                colors.append(rgb)
            # Extract colors from SH coefficients
            elif 'f_dc_0' in prop_names and 'f_dc_1' in prop_names and 'f_dc_2' in prop_names:
                dc_0_idx = prop_names.index('f_dc_0')
                dc_1_idx = prop_names.index('f_dc_1')
                dc_2_idx = prop_names.index('f_dc_2')
                sh_dc = [vertex_data[dc_0_idx], vertex_data[dc_1_idx], vertex_data[dc_2_idx]]
                rgb = [max(0, min(1, (1.0 / (1.0 + math.exp(-x))))) for x in sh_dc]
                colors.append(rgb)

    return positions, normals if normals else None, colors if colors else None


# ============================================================
# === Coordinate Conversion =================================
# ============================================================

def convert_to_unreal_coordinates(positions, normals):
    """Convert coordinates to Unreal Engine coordinate system."""
    if not positions:
        return positions, normals

    x_coords = [p[0] for p in positions]
    y_coords = [p[1] for p in positions]
    z_coords = [p[2] for p in positions]
    x_range = max(x_coords) - min(x_coords)
    y_range = max(y_coords) - min(y_coords)
    z_range = max(z_coords) - min(z_coords)

    print(f"[SplatCreator] Coordinate spans → X:{x_range:.3f}, Y:{y_range:.3f}, Z:{z_range:.3f}")
    if z_range < 0.001:
        print("[SplatCreator] Flat plane detected — swapping Y/Z")
        for i in range(len(positions)):
            positions[i][1], positions[i][2] = positions[i][2], positions[i][1]
            if normals:
                normals[i][1], normals[i][2] = normals[i][2], normals[i][1]

    print("[SplatCreator] Converting to Unreal Engine coordinate system...")
    for i in range(len(positions)):
        x, y, z = positions[i]
        positions[i] = [z, x, y]
        if normals:
            nx, ny, nz = normals[i]
            normals[i] = [nz, nx, ny]

    return positions, normals


# ============================================================
# === Downsample =============================================
# ============================================================

def downsample_data(positions, normals, colors, target_count=10000):
    if len(positions) <= target_count:
        return positions, normals, colors
    step = max(1, len(positions)//target_count)
    positions = positions[::step]
    if normals:
        normals = normals[::step]
    if colors:
        colors = colors[::step]
    return positions, normals, colors


# ============================================================
# === Geometry Nodes–like Remeshing ==========================
# ============================================================

def write_obj_with_vertex_colors(mesh, output_path):
    """Write OBJ file with vertex colors embedded in vertex definitions and create MTL file."""
    vertices = np.asarray(mesh.vertices)
    triangles = np.asarray(mesh.triangles)
    colors = np.asarray(mesh.vertex_colors) if mesh.has_vertex_colors() else None
    
    # Create MTL file path
    mtl_path = output_path.replace('.obj', '.mtl')
    mtl_name = os.path.basename(mtl_path)
    
    with open(output_path, 'w') as f:
        f.write("# OBJ file with vertex colors\n")
        f.write(f"# Vertices: {len(vertices)}\n")
        f.write(f"# Faces: {len(triangles)}\n")
        f.write(f"mtllib {mtl_name}\n")
        f.write("usemtl VertexColorMaterial\n\n")

        def to_srgb(c):
            return max(0, min(1, c ** (1.0/2.2)))

        
        # Write vertices with colors
        for i, v in enumerate(vertices):
            if colors is not None and i < len(colors):
                # Write vertex with RGB color (0-1 range)
                cr, cg, cb = colors[i]
                cr, cg, cb = to_srgb(cr), to_srgb(cg), to_srgb(cb)

                f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f} {cr:.6f} {cg:.6f} {cb:.6f}\n")

            else:
                # Write vertex without color
                f.write(f"v {v[0]:.6f} {v[1]:.6f} {v[2]:.6f}\n")
        
        # Write faces (OBJ uses 1-based indexing)
        for tri in triangles:
            f.write(f"f {tri[0]+1} {tri[1]+1} {tri[2]+1}\n")
    
    # Create MTL file for vertex colored mesh
    with open(mtl_path, 'w') as f:
        f.write("# Material file for vertex colored mesh\n")
        f.write("# This material uses vertex colors embedded in the OBJ file\n")
        f.write("newmtl VertexColorMaterial\n")
        f.write("# Standard material properties\n")
        f.write("Ka 1.0 1.0 1.0\n")
        f.write("Kd 1.0 1.0 1.0\n")
        f.write("Ks 0.0 0.0 0.0\n")
        f.write("Ns 0.0\n")
        f.write("d 1.0\n")
        f.write("# Vertex colors are embedded in OBJ vertex definitions\n")
    
    print(f"[SplatCreator] OBJ with vertex colors written: {output_path}")
    print(f"[SplatCreator] MTL file created: {mtl_path}")

def geometry_nodes_like_remesh(positions, colors=None, poisson_depth=10, smooth_iterations=5, voxel_downsample=0.01):
    """Emulate Blender 'Points to Volume → Volume to Mesh' with color transfer.
    
    Args:
        positions: Point positions
        colors: Point colors (optional)
        poisson_depth: Depth for Poisson reconstruction (higher = more detail, 8-12 recommended)
        smooth_iterations: Number of smoothing iterations (higher = smoother)
        voxel_downsample: Voxel size for downsampling (smaller = more detail, 0 = no downsample)
    """
    positions_array = np.array(positions)
    if colors:
        colors_array = np.array(colors)
    
    # Build Open3D point cloud
    pcd = o3d.geometry.PointCloud()
    pcd.points = o3d.utility.Vector3dVector(positions_array)
    if colors:
        pcd.colors = o3d.utility.Vector3dVector(colors_array)
    
    #add mising surface
    print("[SplatCreator] Removing statistical outliers…")
    pcd, _ = pcd.remove_statistical_outlier(nb_neighbors=40, std_ratio=1.8)

    # Upsample splats to fill thin areas (this is the key fix)
    print("[SplatCreator] Upsampling point cloud...")
    pcd = pcd.uniform_down_sample(every_k_points=1)  # keep original
    pcd = pcd.voxel_down_sample(0.005)  # re-densify
    pcd.orient_normals_consistent_tangent_plane(k=40)

    #downsampling
    if voxel_downsample > 0:
        pcd = pcd.voxel_down_sample(voxel_size=voxel_downsample)
        print(f"[SplatCreator] Voxel downsampled to {len(pcd.points)} points")
    
    # Estimate normals with improved parameters for better detail
    pcd.estimate_normals(
        search_param=o3d.geometry.KDTreeSearchParamHybrid(radius=0.15, max_nn=50)  # Larger radius, more neighbors = better normals
    )
    pcd.orient_normals_consistent_tangent_plane(k=20)  # Increased from 15 to 20 for better consistency

    # Create surface (Poisson) with higher quality
    print(f"[SplatCreator] Running Poisson reconstruction (depth={poisson_depth})...")
    mesh, densities = o3d.geometry.TriangleMesh.create_from_point_cloud_poisson(
        pcd, depth=poisson_depth, width=0, scale=1.1, linear_fit=False
    )
    
    # Remove low-density vertices (cleanup outlier geometry) - even less aggressive to preserve detail
    densities = np.asarray(densities)
    density_threshold = np.quantile(densities, 0.0005)  # Remove only bottom 0.05% (was 0.1%) to preserve more detail
    vertices_to_remove = densities < density_threshold
    mesh.remove_vertices_by_mask(vertices_to_remove)
    
    # Clean up mesh
    mesh.remove_degenerate_triangles()
    mesh.remove_duplicated_triangles()
    mesh.remove_duplicated_vertices()
    mesh.remove_non_manifold_edges()
    
    # Apply Laplacian smoothing for cleaner surfaces
    if smooth_iterations > 0:
        print(f"[SplatCreator] Applying Laplacian smoothing ({smooth_iterations} iterations)...")
        mesh = mesh.filter_smooth_laplacian(number_of_iterations=smooth_iterations)
    
    # Transfer colors from point cloud to mesh vertices
    if colors:
        print("[SplatCreator] Transferring vertex colors...")
        mesh_vertices = np.asarray(mesh.vertices)
        pcd_points = np.asarray(pcd.points)
        pcd_colors = np.asarray(pcd.colors)
        
        # Filter out any NaN or inf values
        valid_mask = np.all(np.isfinite(mesh_vertices), axis=1)
        if not np.all(valid_mask):
            print(f"[SplatCreator] Warning: Removing {np.sum(~valid_mask)} invalid vertices")
            mesh.remove_vertices_by_mask(~valid_mask)
            mesh_vertices = np.asarray(mesh.vertices)
        
        tree = cKDTree(pcd_points)
        distances, indices = tree.query(mesh_vertices, k=1)
        mesh_colors = pcd_colors[indices]
        mesh.vertex_colors = o3d.utility.Vector3dVector(mesh_colors)
    
    print(f"[SplatCreator] Remeshed surface: {len(mesh.vertices)} vertices, {len(mesh.triangles)} triangles")
    return mesh


# ============================================================
# === Main Pipeline ==========================================
# ============================================================

def convert_gaussian_ply_to_obj_and_remesh(ply_file, output_path):
    # Determine output directory
    if output_path.lower().endswith('.obj'):
        output_dir = os.path.dirname(output_path)
        if output_path.lower().endswith('_mesh.obj'):
            remeshed_path = output_path
        else:
            remeshed_path = output_path.replace('.obj', '_mesh.obj')

    else:
        output_dir = output_path
        remeshed_path = os.path.join(output_path, "gaussian_mesh.obj")

    os.makedirs(output_dir, exist_ok=True)

    print(f"[SplatCreator] Reading PLY: {ply_file}")
    vertex_count, properties = read_ply_header(ply_file)
    positions, normals, colors = read_ply_data(ply_file, vertex_count, properties)
    print(f"[SplatCreator] Loaded {len(positions)} vertices")

    positions, normals = convert_to_unreal_coordinates(positions, normals)

    # Keep maximum detail for high-quality remesh - increased limit for more detail
    if len(positions) > 800000:
        positions, normals, colors = downsample_data(positions, normals, colors, 800000)
        print(f"[SplatCreator] Downsampled to {len(positions)}")


    # Skip original OBJ, go straight to remesh
    print("[SplatCreator] Performing volumetric remesh...")
    # Parameters optimized for maximum detail
    mesh = geometry_nodes_like_remesh(
        positions, 
        colors, 
        poisson_depth=13,       # Very high detail
        smooth_iterations=1,    # Minimal smoothing to preserve detail
        voxel_downsample=0.001  # Very fine voxel resolution
    )
    
    # Use custom OBJ writer that properly embeds vertex colors
    write_obj_with_vertex_colors(mesh, remeshed_path)
    print(f"[SplatCreator] Remeshed OBJ saved: {remeshed_path}")

    return True


# ============================================================
# === Entry Point ============================================
# ============================================================

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 convert_gaussian_ply.py <input.ply> <output.obj|output_dir>")
        sys.exit(1)
    ply_file = sys.argv[1]
    output_path = sys.argv[2]
    convert_gaussian_ply_to_obj_and_remesh(ply_file, output_path)
