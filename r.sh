SRC_DIR="./src"
BUILD_DIR="./build"

mkdir -p $BUILD_DIR

# INPUT_SHADER_DIR="$SRC_DIR/shaders/"
# OUTPUT_SHADER_DIR="$BUILD_DIR/shaders/"
# slangc src/shaders/shader.slang -target spirv -profile spirv_1_4 -emit-spirv-directly -fvk-use-entrypoint-name -entry vertMain -entry fragMain -o build/shaders/slang.spv

EXEC="app"
gcc -g3 $SRC_DIR/main.c -o $BUILD_DIR/$EXEC -lglfw -lvulkan -Wall -Wextra -Wpedantic -Werror -Wconversion -Wsign-conversion -fanalyzer -fsanitize=address,undefined -fno-omit-frame-pointer -DDEBUG -DVK_ENABLE_VALIDATION

./build/app


