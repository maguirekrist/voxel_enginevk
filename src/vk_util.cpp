#include "vk_util.h"
#include "vk_initializers.h"
#include <algorithm>


std::string getShaderPath(const std::string& shaderName) {
	// Get the executable path
	std::filesystem::path exePath = std::filesystem::current_path();

	// Navigate to the shaders directory
	return (exePath / "shaders" / shaderName).string();
}

bool vkutil::load_shader_module(const std::string& filePath, VkDevice device, VkShaderModule* outShaderModule)
{

	const auto& path = getShaderPath(filePath);

	//open the file. With cursor at the end
	std::ifstream file(path, std::ios::ate | std::ios::binary);

	if (!file.is_open()) {
		std::println("Unable to find file at path: {}", path);
		return false;
	}


	//find what the size of the file is by looking up the location of the cursor
	//because the cursor is at the end, it gives the size directly in bytes
	size_t fileSize = (size_t)file.tellg();

	//spirv expects the buffer to be on uint32, so make sure to reserve an int vector big enough for the entire file
	std::vector<uint32_t> buffer(fileSize / sizeof(uint32_t));

	//put file cursor at beginning
	file.seekg(0);

	//load the entire file into the buffer
	file.read((char*)buffer.data(), fileSize);

	//now that the file is loaded into the buffer, we can close it
	file.close();

	//create a new shader module, using the buffer we loaded
	VkShaderModuleCreateInfo createInfo = {};
	createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	createInfo.pNext = nullptr;

	//codeSize has to be in bytes, so multiply the ints in the buffer by size of int to know the real size of the buffer
	createInfo.codeSize = buffer.size() * sizeof(uint32_t);
	createInfo.pCode = buffer.data();

	//check that the creation goes well.
	VkShaderModule shaderModule;
	auto result = vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    if (result != VK_SUCCESS)
    {
        //fmt::println("Could not create shader module Vulkan Error {}", result);
        return false;
    }

	*outShaderModule = shaderModule;
	return true;
}

void vkutil::transition_image(VkCommandBuffer cmd, VkImage image, VkImageLayout currentLayout, VkImageLayout targetLayout)
{
	// Define an image memory barrier
	VkImageMemoryBarrier barrier{};
	barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
	barrier.oldLayout = currentLayout; // The layout used in the off-screen render pass
	barrier.newLayout = targetLayout; // The layout for compute shader access
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;

	// Define the aspect mask based on the image format (assumed to be color here)
	barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
	barrier.subresourceRange.baseMipLevel = 0;
	barrier.subresourceRange.levelCount = 1;
	barrier.subresourceRange.baseArrayLayer = 0;
	barrier.subresourceRange.layerCount = 1;

	// Set up access masks for synchronization
	barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; // Previous access mask
	barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT; // Access needed for compute shader

	// Insert the pipeline barrier
	vkCmdPipelineBarrier(
		cmd,
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, // Previous stage
		VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,          // Stage where the compute shader reads the image
		0,                                             // No additional flags
		0, nullptr,                                    // No memory barriers
		0, nullptr,                                    // No buffer barriers
		1, &barrier                                    // One image memory barrier
	);
}

AllocatedBuffer vkutil::create_buffer(VmaAllocator allocator, size_t size, VkBufferUsageFlags bufferUsage, VmaMemoryUsage memUsage)
{
	VkBufferCreateInfo bufferInfo = vkinit::buffer_create_info(size, bufferUsage);

	std::string debugMsg = "THIS IS A TEST";

	VmaAllocationCreateInfo vmaallocInfo = {};
	vmaallocInfo.usage = memUsage;
	vmaallocInfo.pUserData = static_cast<void*>(const_cast<char*>(debugMsg.c_str()));

	AllocatedBuffer buffer = {};

	vmaCreateBuffer(allocator, &bufferInfo, &vmaallocInfo,
		&buffer._buffer,
		&buffer._allocation,
		nullptr);

	buffer._size = size;

	return buffer;
}

AllocatedImage vkutil::create_image(VmaAllocator allocator, VkExtent3D extent, VkFormat format, VkImageUsageFlags usage, VmaMemoryUsage memUsage)
{
    VkImageCreateInfo imageInfo = vkinit::image_create_info(format, usage, extent);

    VmaAllocationCreateInfo allocInfo = {
		.usage = memUsage,
		.requiredFlags = VkMemoryPropertyFlags(VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT),
	};

	AllocatedImage image;
	vmaCreateImage(allocator, &imageInfo, &allocInfo, &image._image, &image._allocation, nullptr);
	
	return image;
}

VkDescriptorPool createPool(VkDevice device, const vkutil::DescriptorAllocator::PoolSizes& poolSizes, int count, VkDescriptorPoolCreateFlags flags)
{
		std::vector<VkDescriptorPoolSize> sizes;
		sizes.reserve(poolSizes.sizes.size());
		for (auto sz : poolSizes.sizes) {
			sizes.push_back({ sz.first, uint32_t(sz.second * count) });
		}
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = flags;
		pool_info.maxSets = count;
		pool_info.poolSizeCount = (uint32_t)sizes.size();
		pool_info.pPoolSizes = sizes.data();

		VkDescriptorPool descriptorPool;
		vkCreateDescriptorPool(device, &pool_info, nullptr, &descriptorPool);

		return descriptorPool;
}


void vkutil::DescriptorAllocator::reset_pools()
{
    //reset all used pools and add them to the free pools
	for (auto p : usedPools){
		vkResetDescriptorPool(device, p, 0);
		freePools.push_back(p);
	}

	//clear the used pools, since we've put them all in the free pools
	usedPools.clear();

	//reset the current pool handle back to null
	currentPool = VK_NULL_HANDLE;
}

bool vkutil::DescriptorAllocator::allocate(VkDescriptorSet *set, VkDescriptorSetLayout layout)
{
    //initialize the currentPool handle if it's null
    if (currentPool == VK_NULL_HANDLE){

        currentPool = grab_pool();
        usedPools.push_back(currentPool);
    }

    VkDescriptorSetAllocateInfo allocInfo = {};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.pNext = nullptr;

    allocInfo.pSetLayouts = &layout;
    allocInfo.descriptorPool = currentPool;
    allocInfo.descriptorSetCount = 1;

    //try to allocate the descriptor set
    VkResult allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);
    bool needReallocate = false;

    switch (allocResult) {
    case VK_SUCCESS:
        //all good, return
        return true;
    case VK_ERROR_FRAGMENTED_POOL:
    case VK_ERROR_OUT_OF_POOL_MEMORY:
        //reallocate pool
        needReallocate = true;
        break;
    default:
        //unrecoverable error
        return false;
    }

    if (needReallocate){
        //allocate a new pool and retry
        currentPool = grab_pool();
        usedPools.push_back(currentPool);

        allocResult = vkAllocateDescriptorSets(device, &allocInfo, set);

        //if it still fails then we have big issues
        if (allocResult == VK_SUCCESS){
            return true;
        }
    }

    return false;
}

void vkutil::DescriptorAllocator::init(VkDevice newDevice)
{
    device = newDevice;
}

void vkutil::DescriptorAllocator::cleanup()
{
    for (auto p : freePools)
	{
		vkDestroyDescriptorPool(device, p, nullptr);
	}
	for (auto p : usedPools)
	{
		vkDestroyDescriptorPool(device, p, nullptr);
	}   
}

VkDescriptorPool vkutil::DescriptorAllocator::grab_pool()
{
    //there are reusable pools availible
	if (freePools.size() > 0)
	{
		//grab pool from the back of the vector and remove it from there.
		VkDescriptorPool pool = freePools.back();
		freePools.pop_back();
		return pool;
	}
	else
	{
		//no pools availible, so create a new one
		return createPool(device, descriptorSizes, 1000, 0);
	}
}


void vkutil::DescriptorLayoutCache::init(VkDevice newDevice){
	device = newDevice;
}
void vkutil::DescriptorLayoutCache::cleanup(){
	//delete every descriptor layout held
	for (auto pair : layoutCache){
		vkDestroyDescriptorSetLayout(device, pair.second, nullptr);
	}
}




VkDescriptorSetLayout vkutil::DescriptorLayoutCache::create_descriptor_layout(VkDescriptorSetLayoutCreateInfo* info){
	DescriptorLayoutInfo layoutinfo;
	layoutinfo.bindings.reserve(info->bindingCount);
	bool isSorted = true;
	int lastBinding = -1;

	//copy from the direct info struct into our own one
	for (int i = 0; i < info->bindingCount; i++) {
		layoutinfo.bindings.push_back(info->pBindings[i]);

		//check that the bindings are in strict increasing order
		if (info->pBindings[i].binding > lastBinding){
			lastBinding = info->pBindings[i].binding;
		}
		else{
			isSorted = false;
		}
	}
	//sort the bindings if they aren't in order
	if (!isSorted){
		std::sort(layoutinfo.bindings.begin(), layoutinfo.bindings.end(), [](VkDescriptorSetLayoutBinding& a, VkDescriptorSetLayoutBinding& b ){
				return a.binding < b.binding;
		});
	}

	//try to grab from cache
	auto it = layoutCache.find(layoutinfo);
	if (it != layoutCache.end()){
		return (*it).second;
	}
	else {
		//create a new one (not found)
		VkDescriptorSetLayout layout;
		vkCreateDescriptorSetLayout(device, info, nullptr, &layout);

		//add to cache
		layoutCache[layoutinfo] = layout;
		return layout;
		}
}


bool vkutil::DescriptorLayoutCache::DescriptorLayoutInfo::operator==(const DescriptorLayoutInfo& other) const{
	if (other.bindings.size() != bindings.size()){
		return false;
	}
	else {
		//compare each of the bindings is the same. Bindings are sorted so they will match
		for (int i = 0; i < bindings.size(); i++) {
			if (other.bindings[i].binding != bindings[i].binding){
				return false;
			}
			if (other.bindings[i].descriptorType != bindings[i].descriptorType){
				return false;
			}
			if (other.bindings[i].descriptorCount != bindings[i].descriptorCount){
				return false;
			}
			if (other.bindings[i].stageFlags != bindings[i].stageFlags){
				return false;
			}
		}
		return true;
	}
}

size_t vkutil::DescriptorLayoutCache::DescriptorLayoutInfo::hash() const{
    using std::size_t;
    using std::hash;

    size_t result = hash<size_t>()(bindings.size());

    for (const VkDescriptorSetLayoutBinding& b : bindings)
    {
        //pack the binding data into a single int64. Not fully correct but it's ok
        size_t binding_hash = b.binding | b.descriptorType << 8 | b.descriptorCount << 16 | b.stageFlags << 24;

        //shuffle the packed binding data and xor it with the main hash
        result ^= hash<size_t>()(binding_hash);
    }

    return result;
}


vkutil::DescriptorBuilder vkutil::DescriptorBuilder::begin(DescriptorLayoutCache* layoutCache, DescriptorAllocator* allocator){

	DescriptorBuilder builder;

	builder.cache = layoutCache;
	builder.alloc = allocator;
	return builder;
}

vkutil::DescriptorBuilder& vkutil::DescriptorBuilder::bind_buffer(uint32_t binding, VkDescriptorBufferInfo* bufferInfo, VkDescriptorType type, VkShaderStageFlags stageFlags)
{
    //create the descriptor binding for the layout
    VkDescriptorSetLayoutBinding newBinding{};

    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.pImmutableSamplers = nullptr;
    newBinding.stageFlags = stageFlags;
    newBinding.binding = binding;

    bindings.push_back(newBinding);

    //create the descriptor write
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.pBufferInfo = bufferInfo;
    newWrite.dstBinding = binding;

    writes.push_back(newWrite);
    return *this;
}

vkutil::DescriptorBuilder& vkutil::DescriptorBuilder::bind_image(uint32_t binding, VkDescriptorImageInfo *imageInfo, VkDescriptorType type, VkShaderStageFlags stageFlags)
{
    VkDescriptorSetLayoutBinding newBinding{};

    newBinding.descriptorCount = 1;
    newBinding.descriptorType = type;
    newBinding.pImmutableSamplers = nullptr;
    newBinding.stageFlags = stageFlags;
    newBinding.binding = binding;

    bindings.push_back(newBinding);

    //create the descriptor write
    VkWriteDescriptorSet newWrite{};
    newWrite.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    newWrite.pNext = nullptr;

    newWrite.descriptorCount = 1;
    newWrite.descriptorType = type;
    newWrite.pImageInfo = imageInfo;
    newWrite.dstBinding = binding;

    writes.push_back(newWrite);
    return *this;
}

bool vkutil::DescriptorBuilder::build(VkDescriptorSet& set, VkDescriptorSetLayout& layout){
	//build layout first
	VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = nullptr;

	layoutInfo.pBindings = bindings.data();
	layoutInfo.bindingCount = bindings.size();

	layout = cache->create_descriptor_layout(&layoutInfo);

	//allocate descriptor
	bool success = alloc->allocate(&set, layout);
	if (!success) { return false; };

	//write descriptor
	for (VkWriteDescriptorSet& w : writes) {
		w.dstSet = set;
	}

	vkUpdateDescriptorSets(alloc->device, writes.size(), writes.data(), 0, nullptr);

	return true;
}

bool vkutil::DescriptorBuilder::build(VkDescriptorSet& set)
{
		VkDescriptorSetLayoutCreateInfo layoutInfo{};
	layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	layoutInfo.pNext = nullptr;

	layoutInfo.pBindings = bindings.data();
	layoutInfo.bindingCount = bindings.size();

	auto layout = cache->create_descriptor_layout(&layoutInfo);

	//allocate descriptor
	bool success = alloc->allocate(&set, layout);
	if (!success) { return false; };

	//write descriptor
	for (VkWriteDescriptorSet& w : writes) {
		w.dstSet = set;
	}

	vkUpdateDescriptorSets(alloc->device, writes.size(), writes.data(), 0, nullptr);

	return true;
}



