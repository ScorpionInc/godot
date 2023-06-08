/**************************************************************************/
/*  file_access_android.cpp                                               */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#include "file_access_android.h"

#include "core/string/print_string.h"

AAssetManager *FileAccessAndroid::asset_manager = nullptr;

String FileAccessAndroid::get_path() const {
	return path_src;
}

String FileAccessAndroid::get_path_absolute() const {
	return absolute_path;
}

String FileAccessAndroid::fix_path(const String &p_path) const {
	String path = FileAccess::fix_path(p_path).simplify_path();
	if (path.begins_with("/")) {
		path = path.substr(1, path.length());
	} else if (path.begins_with("res://")) {
		path = path.substr(6, path.length());
	}
	return path;
}

Error FileAccessAndroid::open_internal(const String &p_path, int p_mode_flags) {
	_close();

	path_src = p_path;
	absolute_path = FileAccess::fix_path(p_path).simplify_path();
	String path = fix_path(p_path);

	ERR_FAIL_COND_V(p_mode_flags & FileAccess::WRITE, ERR_UNAVAILABLE); //can't write on android..
	asset = AAssetManager_open(asset_manager, path.utf8().get_data(), AASSET_MODE_STREAMING);
	if (!asset) {
		return ERR_CANT_OPEN;
	}
	len = AAsset_getLength(asset);
	pos = 0;
	eof = false;

	return OK;
}

void FileAccessAndroid::_close() {
	if (!asset) {
		return;
	}
	AAsset_close(asset);
	asset = nullptr;
}

bool FileAccessAndroid::is_open() const {
	return asset != nullptr;
}

void FileAccessAndroid::seek(uint64_t p_position) {
	ERR_FAIL_NULL(asset);

	AAsset_seek(asset, p_position, SEEK_SET);
	pos = p_position;
	if (pos > len) {
		pos = len;
		eof = true;
	} else {
		eof = false;
	}
}

void FileAccessAndroid::seek_end(int64_t p_position) {
	ERR_FAIL_NULL(asset);
	AAsset_seek(asset, p_position, SEEK_END);
	pos = len + p_position;
}

uint64_t FileAccessAndroid::get_position() const {
	return pos;
}

uint64_t FileAccessAndroid::get_length() const {
	return len;
}

bool FileAccessAndroid::eof_reached() const {
	return eof;
}

uint8_t FileAccessAndroid::get_8() const {
	if (pos >= len) {
		eof = true;
		return 0;
	}

	uint8_t byte;
	AAsset_read(asset, &byte, 1);
	pos++;
	return byte;
}

uint64_t FileAccessAndroid::get_buffer(uint8_t *p_dst, uint64_t p_length) const {
	ERR_FAIL_COND_V(!p_dst && p_length > 0, -1);

	int r = AAsset_read(asset, p_dst, p_length);

	if (pos + p_length > len) {
		eof = true;
	}

	if (r >= 0) {
		pos += r;
		if (pos > len) {
			pos = len;
		}
	}
	return r;
}

Error FileAccessAndroid::get_error() const {
	return eof ? ERR_FILE_EOF : OK; // not sure what else it may happen
}

void FileAccessAndroid::flush() {
	ERR_FAIL();
}

void FileAccessAndroid::store_8(uint8_t p_dest) {
	ERR_FAIL();
}

bool FileAccessAndroid::file_exists(const String &p_path) {
	AAsset *at = AAssetManager_open(asset_manager, fix_path(p_path).utf8().get_data(), AASSET_MODE_STREAMING);

	if (!at) {
		return false;
	}

	AAsset_close(at);
	return true;
}

void FileAccessAndroid::close() {
	_close();
}

Error FileAccessAndroid::asset_to_file(const String &p_asset_path, const String &p_file_path) {
	//!TODO Validate output file
	// Outputs Android APK Asset to writable file (if possible) using asset buffer mode(better chunking).
	// Open the asset file
	AAsset* asset_file = AAssetManager_open(asset_manager, fix_path(p_asset_path).utf8().get_data(), AASSET_MODE_BUFFER);
	if (!at) {
		ERR_PRINT("Conversion from Android asset to file failed to open asset at path.");
		return ERR_CANT_OPEN;
	}
	// Get the file length
	size_t file_length = AAsset_getLength(asset_file);

	// Allocate memory and read the file
	char* file_content = new char[file_length + 1];
	int amount_read = AAsset_read(asset_file, file_content, file_length);
	// For safety we add a 0 terminating character at the end
	file_content[file_length] = '\0';
	if (amount_read != file_length) {
		ERR_PRINT("Conversion from Android asset to file failed to read expected number of bytes.");
		delete [] file_content;
		return ERR_FILE_EOF;
	}

	// Open output file to save content to
	FILE *out = fopen(p_file_path.utf8().get_data(), "wb");
	if (out == nullptr) {
		ERR_PRINT("Conversion from Android asset to file failed to open the destination file to save asset to.");
		return ERR_UNAVAILABLE;
	}
	fwrite(file_content, file_length, 1, out);
	fclose(out);

	// Free the memory and close the asset.
	delete [] file_content;
	AAsset_close(asset);
	return OK;
}

Error FileAccessAndroid::save_to_file(const String &p_file_path) {
	//TODO
	// Outputs Android APK Asset to writable file (if possible) using asset streaming mode(manual chunking).

	// Open output file to save content to
	FILE *out = fopen(p_file_path.utf8().get_data(), "wb");
	if (out == nullptr) {
		ERR_PRINT("Conversion from Android asset to file failed to open the destination file to save asset to.");
		return ERR_UNAVAILABLE;
	}

	return OK;
}

FileAccessAndroid::~FileAccessAndroid() {
	_close();
}
