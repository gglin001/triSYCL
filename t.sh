docker run -d -it \
  --name cl_dev_0 \
  -v $PWD/../:/repos \
  -w /repos \
  base:latest

# need latest gcc/gxx
micromamba install gcc gxx
# optional, skip some tests for now
# apt install --no-install-recommends libgtkmm-3.0-dev

cmake \
  --no-warn-unused-cli \
  -DCMAKE_BUILD_TYPE:STRING=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS:BOOL=TRUE \
  -DCMAKE_C_COMPILER:FILEPATH=$CONDA_PREFIX/bin/gcc \
  -DCMAKE_CXX_COMPILER:FILEPATH=$CONDA_PREFIX/bin/g++ \
  -S $PWD -B $PWD/build -G Ninja

cmake --build $PWD/build --config Debug --target all --

# test
build/tests/examples/examples_vector_add
