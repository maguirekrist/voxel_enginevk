#include <csignal>
#include <vk_engine.h>
#include <FastNoise/FastNoise.h>

#define TRACY_MEM_ENABLE 0

#if TRACY_MEM_ENABLE
  #include <tracy/Tracy.hpp>
  #define TRACY_ALLOC(ptr, sz) TracyAlloc(ptr, sz)
  #define TRACY_FREE(ptr)      TracyFree(ptr)
#else
  #define TRACY_ALLOC(ptr, sz) ((void)0)
  #define TRACY_FREE(ptr)      ((void)0)
#endif

#if TRACY_MEM_ENABLE
#include <tracy/Tracy.hpp>
#include <cstdlib>
#include <new>

void* operator new(std::size_t sz) { void* p = std::malloc(sz); TRACY_ALLOC(p, sz); if(!p) throw std::bad_alloc(); return p; }
void  operator delete(void* p) noexcept { if(p) TRACY_FREE(p); std::free(p); }
void  operator delete(void* p, std::size_t) noexcept { if(p) TRACY_FREE(p); std::free(p); }

#if __cpp_aligned_new
static void* aligned_alloc_portable(std::size_t a, std::size_t sz) { void* p=nullptr; (void)posix_memalign(&p,a,(sz+a-1)/a*a); return p; }
void* operator new(std::size_t sz, std::align_val_t al) { auto a=(std::size_t)al; void* p=aligned_alloc_portable(a, sz); TRACY_ALLOC(p, sz); if(!p) throw std::bad_alloc(); return p; }
void  operator delete(void* p, std::align_val_t) noexcept { if(p) TRACY_FREE(p); std::free(p); }
void  operator delete(void* p, std::size_t, std::align_val_t) noexcept { if(p) TRACY_FREE(p); std::free(p); }
#endif

// (optional but safe)
void* operator new[](std::size_t sz){ void* p=std::malloc(sz); TRACY_ALLOC(p, sz); if(!p) throw std::bad_alloc(); return p; }
void  operator delete[](void* p) noexcept { if(p) TRACY_FREE(p); std::free(p); }
void  operator delete[](void* p, std::size_t) noexcept { if(p) TRACY_FREE(p); std::free(p); }
#if __cpp_aligned_new
void* operator new[](std::size_t sz, std::align_val_t al){ auto a=(std::size_t)al; void* p=aligned_alloc_portable(a, sz); TRACY_ALLOC(p, sz); if(!p) throw std::bad_alloc(); return p; }
void  operator delete[](void* p, std::align_val_t) noexcept { if(p) TRACY_FREE(p); std::free(p); }
void  operator delete[](void* p, std::size_t, std::align_val_t) noexcept { if(p) TRACY_FREE(p); std::free(p); }
#endif
#endif

int main(int argc, char* argv[])
{

	//std::raise(SIGTRAP);
	VulkanEngine& engine = VulkanEngine::instance();

	engine.init();	
	
	engine.run();	

	engine.cleanup();	

	return 0;
}
