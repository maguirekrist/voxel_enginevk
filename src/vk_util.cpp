#include "vk_util.h"
#include "vk_initializers.h"
#include <algorithm>

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

AllocatedBuffer vkutil::create_buffer(VmaAllocator allocator, const size_t size, const VkBufferUsageFlags bufferUsage, const VmaMemoryUsage memUsage)
{
	VkBufferCreateInfo bufferInfo = vkinit::buffer_create_info(size, bufferUsage);
	VmaAllocationCreateInfo vma_allocInfo = {};
	vma_allocInfo.usage = memUsage;
	//vma allows you to attach debug messages to allocations, interesting.
	//vmaallocInfo.pUserData = static_cast<void*>(const_cast<char*>(debugMsg.c_str()));
	AllocatedBuffer buffer = {};

	vmaCreateBuffer(allocator, &bufferInfo, &vma_allocInfo,
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

VkDescriptorPool vkutil::DescriptorAllocator::create_pool(const VkDevice device, const vkutil::DescriptorAllocator::PoolSizes& poolSizes, const int count, const VkDescriptorPoolCreateFlags flags)
{
		std::vector<VkDescriptorPoolSize> sizes;
		sizes.reserve(poolSizes.sizes.size());
		for (auto sz : poolSizes.sizes) {
			sizes.push_back({ sz.first, static_cast<uint32_t>(sz.second * count) });
		}
		VkDescriptorPoolCreateInfo pool_info = {};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = flags;
		pool_info.maxSets = count;
		pool_info.poolSizeCount = static_cast<uint32_t>(sizes.size());
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

void vkutil::DescriptorAllocator::cleanup() const
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
	if (freePools.size() > 0)
	{
		VkDescriptorPool pool = freePools.back();
		freePools.pop_back();
		return pool;
	}

	auto new_pool = create_pool(device, descriptorSizes, 1000, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT);
	usedPools.push_back(new_pool);
	return new_pool;
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


