SDL2_PATH=$(brew --prefix sdl2)

mkdir -p /Users/mkrist/dev/vulkan-guide/third_party/SDL_PATH/include/SDL2
mkdir -p /Users/mkrist/dev/vulkan-guide/third_party/SDL_PATH/lib

cp -R $SDL2_PATH/include/SDL2/* /Users/mkrist/dev/vulkan-guide/third_party/SDL_PATH/include/SDL2/
cp $SDL2_PATH/lib/* /Users/mkrist/dev/vulkan-guide/third_party/SDL_PATH/lib/