{ pkgs ? import <nixpkgs> {} }:

let
  nixGL = import (fetchTarball "https://github.com/guibou/nixGL/archive/main.tar.gz") {};
in pkgs.mkShell rec {
	nativeBuildInputs = with pkgs; [
		nixGL.auto.nixGLDefault
		pkg-config
		(lib.hiPrio llvmPackages_latest.clang-tools.override {
			enableLibcxx = false;
		})
		llvmPackages_latest.lldb
		llvmPackages_latest.libllvm
		llvmPackages_latest.libcxx
		llvmPackages_latest.libstdcxxClang
		gdb
		bear
		catch2_3
		cmake
		glslang
		shaderc
		shader-slang
		vulkan-tools
		vulkan-tools-lunarg
		vulkan-validation-layers
		valgrind
		renderdoc
		tracy
	];
	buildInputs = with pkgs; [
		(SDL2.overrideAttrs { enableDebugging = true; })
		glm
		shaderc.lib
		vulkan-headers
		vulkan-loader
		vulkan-memory-allocator
		vk-bootstrap
		boost

		# -----
		xorg.libX11
		xorg.libXrandr
		wayland
		# -----
	];

	LD_LIBRARY_PATH = pkgs.lib.makeLibraryPath buildInputs;
	VULKAN_SDK = "${pkgs.vulkan-headers}";
	VK_LAYER_PATH = "${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d";
}
