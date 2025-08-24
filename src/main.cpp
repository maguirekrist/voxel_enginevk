#include <csignal>
#include <vk_engine.h>

#include <FastNoise/FastNoise.h>

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
