/**************************************************************************/
/*  streamline_installer.cpp                                              */
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

#include "streamline_installer.h"

#include "core/io/dir_access.h"
#include "core/io/file_access.h"
#include "core/io/zip_io.h"
#include "core/object/callable_mp.h"
#include "core/os/os.h"
#include "core/os/time.h"
#include "editor/editor_node.h"
#include "editor/editor_string_names.h"
#include "editor/file_system/editor_paths.h"
#include "editor/themes/editor_scale.h"
#include "scene/gui/box_container.h"
#include "scene/gui/check_box.h"
#include "scene/gui/link_button.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/main/http_request.h"

// The SDK version this source is built against (see thirdparty/streamline/include/sl_version.h).
// When it changes, update the URLs, size and SHA-256 together.
static const char *SL_SDK_VERSION = "2.10.0";
static const char *SL_DOWNLOAD_URL = "https://github.com/NVIDIA-RTX/Streamline/releases/download/v2.10.0/streamline-sdk-v2.10.0.zip";
static const char *SL_DOWNLOAD_FILE = "streamline-sdk-v2.10.0.zip";
static const char *SL_DOWNLOAD_SHA256 = "b0cc810c86b0335d4fd76a02d35fff53e528d2fb66c29860565130f76c9383b0";
static const int64_t SL_DOWNLOAD_SIZE = 213131430;
static const char *SL_RELEASE_URL = "https://github.com/NVIDIA-RTX/Streamline/releases/tag/v2.10.0";
static const char *SL_PRODUCT_URL = "https://developer.nvidia.com/rtx/streamline";
static const char *SL_LICENSE_URL = "https://github.com/NVIDIA-RTX/Streamline/blob/v2.10.0/license.txt";
static const char *SL_THIRD_PARTY_URL = "https://github.com/NVIDIA-RTX/Streamline/blob/v2.10.0/3rd-party-licenses.md";
static const char *DLSS_LICENSE_URL = "https://github.com/NVIDIA/DLSS/blob/main/LICENSE.txt";
static const char *FORK_README_URL = "https://github.com/Diginaat/godot#5-optional-add-the-nvidia-streamline-dlls-only-for-dlss";

// Folder inside the SDK zip with the signed release DLLs. bin/x64/development
// holds unsigned debug builds and is never installed.
static const char *SL_ZIP_DIR = "bin/x64/";
static const char *SL_FILES[] = {
	"sl.interposer.dll",
	"sl.common.dll",
	"sl.dlss.dll",
	"sl.dlss_d.dll",
	"sl.dlss_g.dll",
	"sl.reflex.dll",
	"sl.pcl.dll",
	"sl.nis.dll",
	"sl.deepdvc.dll",
	"sl.directsr.dll",
	"sl.nvperf.dll",
	"nvngx_dlss.dll",
	"nvngx_dlssd.dll",
	"nvngx_dlssg.dll",
	"nvngx_deepdvc.dll",
	"NvLowLatencyVk.dll",
	"nvngx_dlss.license.txt",
	"reflex.license.txt",
	"nis.license.txt",
};
static const int SL_FILE_COUNT = std_size(SL_FILES);

VBoxContainer *StreamlineInstaller::_add_block(VBoxContainer *p_parent, const String &p_title) {
	PanelContainer *panel = memnew(PanelContainer);
	panel->set_theme_type_variation("PanelForeground");
	p_parent->add_child(panel);

	VBoxContainer *vb = memnew(VBoxContainer);
	panel->add_child(vb);

	Label *title_label = memnew(Label(p_title));
	title_label->set_theme_type_variation("HeaderSmall");
	vb->add_child(title_label);
	return vb;
}

void StreamlineInstaller::_add_link(VBoxContainer *p_parent, const String &p_text, const String &p_url) {
	LinkButton *link = memnew(LinkButton);
	link->set_text(p_text);
	link->set_uri(p_url);
	link->set_tooltip_text(p_url);
	link->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	p_parent->add_child(link);
}

String StreamlineInstaller::_get_install_dir() const {
	return OS::get_singleton()->get_executable_path().get_base_dir();
}

String StreamlineInstaller::_get_download_path() const {
	return EditorPaths::get_singleton()->get_cache_dir().path_join(SL_DOWNLOAD_FILE);
}

bool StreamlineInstaller::_is_installed() const {
	return FileAccess::exists(_get_install_dir().path_join("sl.interposer.dll"));
}

void StreamlineInstaller::_update_status() {
	if (_is_installed()) {
		status_label->set_text(vformat(TTR("Status: installed. sl.interposer.dll was found in %s. Running the installer again replaces the files with Streamline SDK %s."), _get_install_dir(), SL_SDK_VERSION));
		status_label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("success_color"), EditorStringName(Editor)));
	} else {
		status_label->set_text(vformat(TTR("Status: not installed. sl.interposer.dll was not found in %s. DLSS, Ray Reconstruction, Frame Generation and Reflex are unavailable; everything else works."), _get_install_dir()));
		status_label->add_theme_color_override(SceneStringName(font_color), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
	}
}

void StreamlineInstaller::_update_buttons() {
	const bool busy = state == STATE_DOWNLOADING || state == STATE_VERIFYING || state == STATE_EXTRACTING;
	accept_check->set_disabled(busy);
	install_button->set_disabled(busy || !accept_check->is_pressed());
	install_button->set_text((state == STATE_FAILED || state == STATE_CANCELED) ? TTR("Retry") : TTR("Download and Install"));
	cancel_button->set_visible(busy);
	cancel_button->set_disabled(state != STATE_DOWNLOADING);
	restart_button->set_visible(state == STATE_DONE);
	get_ok_button()->set_text(busy ? TTR("Hide (keeps running)") : TTR("Close"));
}

void StreamlineInstaller::_log(const String &p_text, const Color &p_color) {
	log->push_color(get_theme_color(SNAME("font_disabled_color"), EditorStringName(Editor)));
	log->add_text("[" + Time::get_singleton()->get_time_string_from_system() + "] ");
	log->pop();
	if (p_color != Color()) {
		log->push_color(p_color);
		log->add_text(p_text);
		log->pop();
	} else {
		log->add_text(p_text);
	}
	log->add_newline();
	print_verbose("Streamline installer: " + p_text);
}

void StreamlineInstaller::_fail(const String &p_text) {
	state = STATE_FAILED;
	set_process_internal(false);
	_log(p_text, get_theme_color(SNAME("error_color"), EditorStringName(Editor)));
	_log(TTR("Installation stopped. You can retry, or install manually with the links above."));
	progress_label->set_text(TTR("Failed. See the log."));
	_update_buttons();
}

void StreamlineInstaller::_accept_toggled(bool p_pressed) {
	_log(p_pressed ? TTR("NVIDIA license terms accepted.") : TTR("NVIDIA license terms no longer accepted."));
	progress_label->set_text(p_pressed ? TTR("Ready to install.") : TTR("Accept the license terms to enable the install button."));
	_update_buttons();
}

void StreamlineInstaller::_install_pressed() {
	ERR_FAIL_COND(!accept_check->is_pressed());

	extract_index = 0;
	installed_count = 0;
	progress_bar->set_value(0);

	const String install_dir = _get_install_dir();
	_log(vformat(TTR("Installing NVIDIA Streamline SDK %s into %s"), SL_SDK_VERSION, install_dir));

	// Fail early when the editor folder isn't writable (for example under Program Files).
	const String probe = install_dir.path_join(".streamline_write_test");
	if (FileAccess::open(probe, FileAccess::WRITE).is_null()) {
		_fail(vformat(TTR("Can't write to %s. Move the editor to a folder you can write to, or install the files manually."), install_dir));
		return;
	}
	DirAccess::remove_absolute(probe);
	_log(TTR("Destination folder is writable."));

	const String zip_path = _get_download_path();
	DirAccess::make_dir_recursive_absolute(zip_path.get_base_dir());

	if (FileAccess::exists(zip_path) && FileAccess::get_sha256(zip_path) == SL_DOWNLOAD_SHA256) {
		_log(vformat(TTR("Found a complete download from an earlier attempt: %s. Skipping the download."), zip_path));
		state = STATE_VERIFYING;
		_update_buttons();
		callable_mp(this, &StreamlineInstaller::_verify).call_deferred();
		return;
	}

	_log(vformat(TTR("Downloading %s (%s)"), SL_DOWNLOAD_URL, String::humanize_size(SL_DOWNLOAD_SIZE)));
	_log(vformat(TTR("Saving to %s"), zip_path));
	downloader->set_download_file(zip_path);
	const Error err = downloader->request(SL_DOWNLOAD_URL);
	if (err != OK) {
		_fail(vformat(TTR("Can't start the download (error %d)."), err));
		return;
	}
	state = STATE_DOWNLOADING;
	download_start_msec = OS::get_singleton()->get_ticks_msec();
	progress_label->set_text(TTR("Connecting..."));
	set_process_internal(true);
	_update_buttons();
}

void StreamlineInstaller::_cancel_pressed() {
	if (state != STATE_DOWNLOADING) {
		return;
	}
	downloader->cancel_request();
	DirAccess::remove_absolute(_get_download_path());
	state = STATE_CANCELED;
	set_process_internal(false);
	_log(TTR("Download canceled. The partial file was deleted. Nothing was installed."), get_theme_color(SNAME("warning_color"), EditorStringName(Editor)));
	progress_label->set_text(TTR("Canceled."));
	_update_buttons();
}

void StreamlineInstaller::_restart_pressed() {
	EditorNode::get_singleton()->restart_editor();
}

void StreamlineInstaller::_open_folder_pressed() {
	OS::get_singleton()->shell_show_in_file_manager(_get_install_dir(), true);
}

void StreamlineInstaller::_download_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body) {
	if (state != STATE_DOWNLOADING) {
		return;
	}
	set_process_internal(false);

	if (p_result != HTTPRequest::RESULT_SUCCESS || p_response_code != HTTPClient::RESPONSE_OK) {
		DirAccess::remove_absolute(_get_download_path());
		_fail(vformat(TTR("Download failed (request result %d, HTTP status %d). Check your internet connection."), p_result, p_response_code));
		return;
	}

	progress_bar->set_value(100);
	_log(vformat(TTR("Download finished: %s."), String::humanize_size(downloader->get_downloaded_bytes())));
	state = STATE_VERIFYING;
	_update_buttons();
	// Give the UI a frame to show the log line before hashing blocks briefly.
	callable_mp(this, &StreamlineInstaller::_verify).call_deferred();
}

void StreamlineInstaller::_verify() {
	const String zip_path = _get_download_path();
	progress_label->set_text(TTR("Verifying checksum..."));
	_log(TTR("Verifying the SHA-256 checksum of the download..."));
	const String sha = FileAccess::get_sha256(zip_path);
	if (sha != SL_DOWNLOAD_SHA256) {
		DirAccess::remove_absolute(zip_path);
		_fail(vformat(TTR("Checksum mismatch: expected %s, got %s. The file was deleted and nothing was installed."), SL_DOWNLOAD_SHA256, sha));
		return;
	}
	_log(vformat(TTR("Checksum OK: %s"), sha), get_theme_color(SNAME("success_color"), EditorStringName(Editor)));

	_log(vformat(TTR("Extracting %d files from \"%s\" in the zip..."), SL_FILE_COUNT, SL_ZIP_DIR));
	state = STATE_EXTRACTING;
	progress_bar->set_value(0);
	_update_buttons();
	// One file per frame, so the log and progress bar stay live.
	set_process_internal(true);
}

bool StreamlineInstaller::_extract_file(const String &p_name) {
	const String dest = _get_install_dir().path_join(p_name);
	const String zip_name = String(SL_ZIP_DIR) + p_name;

	Ref<FileAccess> io_fa;
	zlib_filefunc_def io = zipio_create_io(&io_fa);
	unzFile pkg = unzOpen2(_get_download_path().utf8().get_data(), &io);
	if (!pkg) {
		_fail(TTR("Can't open the downloaded zip."));
		return false;
	}
	if (unzLocateFile(pkg, zip_name.utf8().get_data(), 1) != UNZ_OK) {
		unzClose(pkg);
		_fail(vformat(TTR("%s is missing from the zip."), zip_name));
		return false;
	}

	unz_file_info info;
	unzGetCurrentFileInfo(pkg, &info, nullptr, 0, nullptr, 0, nullptr, 0);
	Vector<uint8_t> file_data;
	file_data.resize(info.uncompressed_size);
	unzOpenCurrentFile(pkg);
	const int read = unzReadCurrentFile(pkg, file_data.ptrw(), file_data.size());
	unzCloseCurrentFile(pkg);
	unzClose(pkg);
	if (read != file_data.size()) {
		_fail(vformat(TTR("Can't read %s from the zip."), zip_name));
		return false;
	}

	if (FileAccess::exists(dest)) {
		// A DLL the editor has loaded can't be overwritten on Windows, but it can be renamed.
		const String old = dest + ".old";
		if (FileAccess::exists(old)) {
			DirAccess::remove_absolute(old);
		}
		if (DirAccess::rename_absolute(dest, old) != OK) {
			_fail(vformat(TTR("Can't replace %s. Close other programs that use it, then retry."), dest));
			return false;
		}
		_log(vformat(TTR("Renamed the existing %s to %s (it may be in use; you can delete it after a restart)."), p_name, old.get_file()));
	}

	Ref<FileAccess> f = FileAccess::open(dest, FileAccess::WRITE);
	if (f.is_null()) {
		_fail(vformat(TTR("Can't write %s."), dest));
		return false;
	}
	f->store_buffer(file_data.ptr(), file_data.size());
	_log(vformat(TTR("Installed %s (%s)"), dest, String::humanize_size(file_data.size())));
	return true;
}

void StreamlineInstaller::_extract_next() {
	if (extract_index >= SL_FILE_COUNT) {
		set_process_internal(false);
		state = STATE_DONE;
		DirAccess::remove_absolute(_get_download_path());
		_log(vformat(TTR("Deleted the downloaded zip %s."), _get_download_path()));
		_log(vformat(TTR("Done: %d files installed. Restart the editor to load Streamline, then select DLSS in Project Settings > Rendering > Scaling 3D > Mode."), installed_count), get_theme_color(SNAME("success_color"), EditorStringName(Editor)));
		progress_label->set_text(TTR("Installed. Restart the editor to use DLSS."));
		_update_status();
		_update_buttons();
		return;
	}

	const String name = SL_FILES[extract_index];
	progress_label->set_text(vformat(TTR("Extracting %s (%d/%d)"), name, extract_index + 1, SL_FILE_COUNT));
	if (!_extract_file(name)) {
		return;
	}
	installed_count++;
	extract_index++;
	progress_bar->set_value(100.0 * extract_index / SL_FILE_COUNT);
}

void StreamlineInstaller::_notification(int p_what) {
	switch (p_what) {
		case NOTIFICATION_INTERNAL_PROCESS: {
			if (state == STATE_DOWNLOADING) {
				const int64_t done = downloader->get_downloaded_bytes();
				int64_t total = downloader->get_body_size();
				if (total <= 0) {
					total = SL_DOWNLOAD_SIZE;
				}
				const double seconds = MAX(0.001, (OS::get_singleton()->get_ticks_msec() - download_start_msec) / 1000.0);
				progress_bar->set_value(100.0 * done / total);
				progress_label->set_text(vformat(TTR("Downloading: %s of %s (%s/s)"), String::humanize_size(done), String::humanize_size(total), String::humanize_size(done / seconds)));
			} else if (state == STATE_EXTRACTING) {
				_extract_next();
			}
		} break;
		case NOTIFICATION_VISIBILITY_CHANGED: {
			if (is_visible()) {
				_update_status();
				_update_buttons();
			}
		} break;
	}
}

void StreamlineInstaller::popup_installer() {
	popup_centered_clamped(Size2(860, 800) * EDSCALE, 0.9);
}

StreamlineInstaller::StreamlineInstaller() {
	set_title(TTR("Install NVIDIA DLSS (Streamline SDK)"));

	VBoxContainer *main_vb = memnew(VBoxContainer);
	add_child(main_vb);

	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_vb->add_child(scroll);

	VBoxContainer *blocks = memnew(VBoxContainer);
	blocks->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll->add_child(blocks);

	PackedStringArray file_list;
	for (const char *file : SL_FILES) {
		file_list.push_back(file);
	}

	// Block 1: what and why.
	VBoxContainer *vb = _add_block(blocks, TTR("Why this is a separate step"));
	Label *text = memnew(Label);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	text->set_text(TTR("DLSS, Ray Reconstruction, Frame Generation and Reflex need NVIDIA's Streamline runtime DLLs. NVIDIA's license doesn't allow shipping them with this editor, so you download them yourself, directly from NVIDIA. This is optional: the editor, the path tracer and PhysX work without it."));
	vb->add_child(text);
	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	vb->add_child(status_label);

	// Block 2: exactly what the installer will do.
	vb = _add_block(blocks, TTR("What \"Download and Install\" will do"));
	text = memnew(Label);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	text->set_text(vformat(TTR("1. Download the official Streamline SDK %s release (%s) from NVIDIA's GitHub:\n      %s\n2. Save it temporarily to:\n      %s\n3. Check its SHA-256 checksum. If it doesn't match, delete it and stop:\n      %s\n4. Copy only these %d files from the zip's \"%s\" folder (signed release builds, never the \"development\" debug builds):\n      %s\n5. Put them next to the editor executable, in:\n      %s\n      A file that already exists is first renamed to <name>.old.\n6. Delete the downloaded zip.\n\nNothing else is changed: no registry, no system folders, no project files. You can follow every step in the log below."),
			SL_SDK_VERSION, String::humanize_size(SL_DOWNLOAD_SIZE), SL_DOWNLOAD_URL, _get_download_path(), SL_DOWNLOAD_SHA256, SL_FILE_COUNT, SL_ZIP_DIR, String(", ").join(file_list), _get_install_dir()));
	vb->add_child(text);

	// Block 3: manual install.
	vb = _add_block(blocks, TTR("Prefer to do it yourself?"));
	text = memnew(Label);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	text->set_text(vformat(TTR("Download the Streamline SDK %s zip from NVIDIA below. Copy the files listed above from its \"%s\" folder into the editor folder, then restart the editor."), SL_SDK_VERSION, SL_ZIP_DIR));
	vb->add_child(text);
	_add_link(vb, vformat(TTR("Streamline SDK %s release page (GitHub)"), SL_SDK_VERSION), SL_RELEASE_URL);
	_add_link(vb, TTR("Direct download of the zip"), SL_DOWNLOAD_URL);
	_add_link(vb, TTR("NVIDIA Streamline product page"), SL_PRODUCT_URL);
	_add_link(vb, TTR("Step-by-step instructions (README of this build)"), FORK_README_URL);
	Button *open_folder = memnew(Button(TTR("Open Editor Folder")));
	open_folder->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	open_folder->connect(SceneStringName(pressed), callable_mp(this, &StreamlineInstaller::_open_folder_pressed));
	vb->add_child(open_folder);

	// Block 4: license terms. Installing is disabled until the user accepts.
	vb = _add_block(blocks, TTR("NVIDIA license terms"));
	text = memnew(Label);
	text->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	text->set_text(TTR("The downloaded files are NVIDIA software under NVIDIA's licenses, not under Godot's MIT license. DLSS, Ray Reconstruction and Frame Generation are covered by the NVIDIA RTX SDKs License. Reflex, NIS and Nsight Perf have their own NVIDIA licenses; their texts are installed next to the editor. Among other things, these licenses restrict redistribution and require you to be of legal age (or have a guardian's consent). Read them before you continue:"));
	vb->add_child(text);
	_add_link(vb, TTR("NVIDIA RTX SDKs License (DLSS)"), DLSS_LICENSE_URL);
	_add_link(vb, TTR("Streamline SDK license"), SL_LICENSE_URL);
	_add_link(vb, TTR("Streamline third-party licenses"), SL_THIRD_PARTY_URL);
	accept_check = memnew(CheckBox(TTR("I have read and accept NVIDIA's license terms for the Streamline SDK, DLSS, Reflex and NIS.")));
	accept_check->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	accept_check->connect(SceneStringName(toggled), callable_mp(this, &StreamlineInstaller::_accept_toggled));
	vb->add_child(accept_check);

	// Controls, progress and log stay visible below the scrolled blocks.
	main_vb->add_child(memnew(HSeparator));
	HBoxContainer *hb = memnew(HBoxContainer);
	main_vb->add_child(hb);
	install_button = memnew(Button);
	install_button->connect(SceneStringName(pressed), callable_mp(this, &StreamlineInstaller::_install_pressed));
	hb->add_child(install_button);
	cancel_button = memnew(Button(TTR("Cancel Download")));
	cancel_button->connect(SceneStringName(pressed), callable_mp(this, &StreamlineInstaller::_cancel_pressed));
	hb->add_child(cancel_button);
	restart_button = memnew(Button(TTR("Restart Editor")));
	restart_button->connect(SceneStringName(pressed), callable_mp(this, &StreamlineInstaller::_restart_pressed));
	hb->add_child(restart_button);
	progress_label = memnew(Label(TTR("Accept the license terms to enable the install button.")));
	progress_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	progress_label->set_text_overrun_behavior(TextServer::OVERRUN_TRIM_ELLIPSIS);
	hb->add_child(progress_label);

	progress_bar = memnew(ProgressBar);
	main_vb->add_child(progress_bar);

	log = memnew(RichTextLabel);
	log->set_custom_minimum_size(Size2(0, 160) * EDSCALE);
	log->set_scroll_follow(true);
	log->set_selection_enabled(true);
	log->set_context_menu_enabled(true);
	main_vb->add_child(log);

	downloader = memnew(HTTPRequest);
	downloader->set_use_threads(true);
	downloader->connect("request_completed", callable_mp(this, &StreamlineInstaller::_download_completed));
	add_child(downloader);
}
