# EXPLAIN — What Vulkan Already Does

The game's static code continues executing on the CPU. When it sends draw commands to the GS, the new backend batches the commands and the GPU executes the shaders: it writes to graphics memory, samples textures, and outputs pixels. The window presents this image directly, maintaining clear roles for the CPU and GPU.

Using a framebuffer already rendered by the CPU and merely displaying it with Vulkan would not demonstrate a Vulkan renderer. Here, the tests compare memory written by the GPU against a secondary CPU render, and logs confirm the CPU rasterizer is disabled during gameplay. We also captured the window showing track selection and the 3D car.

The initial implementation waits for each submission to complete. This simplifies memory management and allows investigating divergences before optimizing. Waiting for the queue to idle is not sufficient to safely reuse presentation-related resources: we use a dedicated fence to know when those resources are safe to reclaim.

Reaching the Ford Ka confirms genuine execution and rendering progress. It does not confirm that an entire race functions, nor that the pixels accurately match a real PS2. Comparison with the CPU renderer reveals a divergence as early as frame 400; subsequent investigation must isolate its root cause while keeping this boundary explicit. MSVC production builds and their contracts remain preserved.
