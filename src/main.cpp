#include <csignal>
#include <vk_engine.h>
#include <tracy/Tracy.hpp>
#include <FastNoise/FastNoise.h>

#define TRACY_MEM_ENABLE 1

#if TRACY_MEM_ENABLE
void* operator new(std::size_t sz) {
	void* p = std::malloc(sz);
	TracyAlloc(p, sz);
	if (!p) throw std::bad_alloc();
	return p;
}
void operator delete(void* p) noexcept {
	if (p) TracyFree(p);
	std::free(p);
}

// also provide the sized & aligned overloads in C++17/20 builds:
void operator delete(void* p, std::size_t) noexcept { if (p) TracyFree(p); std::free(p); }
void* operator new(std::size_t sz, std::align_val_t al) { void* p = ::aligned_alloc((std::size_t)al, ((sz+(std::size_t)al-1)/(std::size_t)al)*(std::size_t)al); TracyAlloc(p, sz); if(!p) throw std::bad_alloc(); return p; }
void operator delete(void* p, std::align_val_t) noexcept { if (p) TracyFree(p); std::free(p); }
#endif


void test()
{
	auto biomePerlin = FastNoise::New<FastNoise::Simplex>();
    
    auto biomeScale = FastNoise::New<FastNoise::DomainScale>();
    biomeScale->SetSource(biomePerlin);
    biomeScale->SetScale(1/1000.f);
    
    auto biomeFractal = FastNoise::New<FastNoise::FractalFBm>();
    biomeFractal->SetSource(biomePerlin);
    
	for(int x = 0; x < 16; x++)
	{
		for(int z = 0; z < 16; z++)
		{
			for(int y = 0; y < 16; y++)
			{
				float val = biomeFractal->GenSingle3D(x, z, y, 1337);
    
    			std::cout << val << std::endl;
			}
		}
	}
}


int main(int argc, char* argv[])
{

	//std::raise(SIGTRAP);
	VulkanEngine& engine = VulkanEngine::instance();

	engine.init();	
	
	engine.run();	

	engine.cleanup();	

	return 0;
}
