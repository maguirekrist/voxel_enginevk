# Multi-threaded Vulkan Cube Engine

<img width="1696" alt="Screenshot 2024-10-06 at 7 18 14 PM" src="https://github.com/user-attachments/assets/cb5fc0b2-6d06-4243-aef1-dbd90670c5a2">

## Features
- Multi-threaded chunk meshing and generation based on a thread-pool system.
- Tracy profiler setup
- Vulkan 1.1 compliant
- Full-screen post-processing effects in compute shader
- FastNoise2 layered height map generation
- Sunlight direction lighting and ambient occlusion pre-calculated per mesh

## Testing

Run the automated test suite from the repository root with:

```powershell
.\run_tests.ps1 -Release
```

You can also run the default configuration with:

```powershell
.\run_tests.ps1
```

Engineering note:
- Run the test suite after every large initiative or systems-level change before considering the work complete.
