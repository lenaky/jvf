# Environment

1. spdlog
    * tested version spdlog-1.5.0
	* spdlog source code should be placed in jvf/external_libs/spdlog

2. ffmpeg
    * tested version ffmpeg-4.2.2 (x64, For Windows)
	* ffmpeg libs should be placed in jvf/external_libs/ffmpeg
	* copy dlls from ffmpeg/bin to ffmpeg/lib

3. protobuf (binary)
	* tested version protoc-3.11.4
	* protoc.exe should be placed in jvf/external_libs/protoc
	* ex) "$(ProjectDir)../../external_libs/protoc/bin/protoc.exe" -I=$(ProjectDir)proto/ --cpp_out=$(ProjectDir) $(ProjectDir)proto\example.proto
	* ex) %(Filename).pb.h;%(Filename).pb.cc;

4. protobuf (source)
    * tested version protoc-3.11.4
	* download source code for c++
	* extracted jvf/external_libs/protoc_source/
	* make build directory named "out_dir"
	* move out_dir and create solution for vs2019
	    * cmake ..\cmake\ -G "Visual Studio 16 2019"
	* ALL_BUILD
	* run extract_includes.bat

5. json2pb
    * https://github.com/yinqiwen/pbjson.git
	* header and cpp file should be placed in jvf/external_libs/pb2json