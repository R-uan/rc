rm -rf ./build
cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=On -DCMAKE_BUILD_TYPE=Debug -B build
cmake --build build
echo "Project built"
./build/rc  # Now ASan will show file:line numbers!
            
