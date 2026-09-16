1. Create directory `EmbeddedCore`
`mkdir -p EmbeddedCore`
2. Clone submodule
`git submodule add -f https://github.com/tuong194/IOTCore.git EmbeddedCore/IOTCoreBase`
3. set IOT_CORE_PATH
`export IOT_CORE_PATH="$PWD/EmbeddedCore/IOTCoreBase"`
test `echo $IOT_CORE_PATH`
