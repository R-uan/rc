if [ ! -d "./build/" ]; then
	mkdir build
fi

cmake -S . -B ./build -G "Ninja" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build ./build

echo "Project built"
            
