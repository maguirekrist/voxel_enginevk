#include <vk_engine.h>

int main(int argc, char* argv[])
{
	VulkanEngine& engine = VulkanEngine::instance();

	engine.init();	
	
	engine.run();	

	engine.cleanup();	

	return 0;
}
