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
#include "scene/gui/margin_container.h"
#include "scene/gui/panel_container.h"
#include "scene/gui/progress_bar.h"
#include "scene/gui/rich_text_label.h"
#include "scene/gui/scroll_container.h"
#include "scene/gui/separator.h"
#include "scene/gui/texture_rect.h"
#include "scene/main/http_request.h"
#include "scene/resources/style_box_flat.h"

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

VBoxContainer *StreamlineInstaller::_add_card(VBoxContainer *p_parent, const String &p_title, const StringName &p_icon, const StringName &p_color) {
	Card card;
	card.icon_name = p_icon;
	card.color_name = p_color;

	card.panel = memnew(PanelContainer);
	p_parent->add_child(card.panel);

	VBoxContainer *vb = memnew(VBoxContainer);
	vb->add_theme_constant_override("separation", 8 * EDSCALE);
	card.panel->add_child(vb);

	HBoxContainer *header = memnew(HBoxContainer);
	header->add_theme_constant_override("separation", 8 * EDSCALE);
	vb->add_child(header);
	card.icon = memnew(TextureRect);
	card.icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	header->add_child(card.icon);
	card.title = memnew(Label(p_title));
	card.title->set_theme_type_variation("HeaderSmall");
	header->add_child(card.title);

	cards.push_back(card);
	return vb;
}

RichTextLabel *StreamlineInstaller::_add_text(VBoxContainer *p_parent) {
	RichTextLabel *rtl = memnew(RichTextLabel);
	rtl->set_use_bbcode(true);
	rtl->set_fit_content(true);
	rtl->set_scroll_active(false);
	rtl->set_selection_enabled(true);
	rtl->set_context_menu_enabled(true);
	rtl->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	rtl->connect("meta_clicked", callable_mp(this, &StreamlineInstaller::_meta_clicked));
	p_parent->add_child(rtl);
	texts.push_back(rtl);
	return rtl;
}

Ref<StyleBox> StreamlineInstaller::_make_card_style(const Color &p_edge, float p_tint) const {
	const Color base = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	Ref<StyleBoxFlat> style;
	style.instantiate();
	style->set_bg_color(base.lerp(p_edge, p_tint));
	style->set_border_color(p_edge);
	style->set_border_width(SIDE_LEFT, MAX(1, int(4 * EDSCALE)));
	style->set_corner_radius_all(6 * EDSCALE);
	style->set_content_margin(SIDE_LEFT, 16 * EDSCALE);
	style->set_content_margin(SIDE_RIGHT, 14 * EDSCALE);
	style->set_content_margin(SIDE_TOP, 12 * EDSCALE);
	style->set_content_margin(SIDE_BOTTOM, 12 * EDSCALE);
	return style;
}

String StreamlineInstaller::_code(const String &p_text) const {
	const Color base = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	const Color font = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	return "[bgcolor=" + base.lerp(font, 0.12).to_html(false) + "][code] " + p_text.replace("[", "[lb]") + " [/code][/bgcolor]";
}

String StreamlineInstaller::_link(const String &p_label, const String &p_url) const {
	const Color font = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const Color base = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	// Show the real address under each link, so nobody clicks blind.
	return "[url=" + p_url + "][b]" + p_label + "[/b][/url]\n[color=" + font.lerp(base, 0.4).to_html(false) + "][code]" + p_url.replace("[", "[lb]") + "[/code][/color]";
}

void StreamlineInstaller::_meta_clicked(const Variant &p_meta) {
	// Never open the browser without asking: show the address first.
	pending_url = p_meta;
	link_confirm->set_text(vformat(TTR("Open this link in your web browser?\n\n%s\n\nThis leaves the editor and connects to that website."), pending_url));
	link_confirm->reset_size();
	link_confirm->popup_centered();
}

void StreamlineInstaller::_open_pending_link() {
	_log(vformat(TTR("Opened %s in the web browser."), pending_url));
	OS::get_singleton()->shell_open(pending_url);
}

void StreamlineInstaller::_update_theme() {
	const Color base = get_theme_color(SNAME("base_color"), EditorStringName(Editor));
	const Color font = get_theme_color(SceneStringName(font_color), SNAME("Label"));
	const Color accent = get_theme_color(SNAME("accent_color"), EditorStringName(Editor));
	const Color success = get_theme_color(SNAME("success_color"), EditorStringName(Editor));
	const Color warning = get_theme_color(SNAME("warning_color"), EditorStringName(Editor));
	const String dim = font.lerp(base, 0.3).to_html(false);
	const String accent_html = accent.to_html(false);
	const String success_html = success.to_html(false);
	const String warning_html = warning.to_html(false);
	const Ref<Font> mono = get_theme_font(SNAME("source"), EditorStringName(EditorFonts));
	const Ref<Font> bold = get_theme_font(SNAME("bold"), EditorStringName(EditorFonts));
	const int font_size = get_theme_font_size(SNAME("main_size"), EditorStringName(EditorFonts));

	for (const Card &card : cards) {
		const Color edge = get_theme_color(card.color_name, EditorStringName(Editor));
		card.panel->add_theme_style_override(SceneStringName(panel), _make_card_style(edge, 0.07));
		card.icon->set_texture(get_editor_theme_icon(card.icon_name));
		card.icon->set_modulate(edge);
		card.title->add_theme_color_override(SceneStringName(font_color), edge.lerp(font, 0.35));
	}
	for (RichTextLabel *rtl : texts) {
		rtl->add_theme_font_override("mono_font", mono);
		rtl->add_theme_font_size_override("mono_font_size", font_size);
		rtl->add_theme_font_override("bold_font", bold);
		rtl->add_theme_font_size_override("bold_font_size", font_size);
		rtl->add_theme_constant_override("line_separation", 3 * EDSCALE);
		rtl->add_theme_constant_override("paragraph_separation", 6 * EDSCALE);
	}
	log->add_theme_font_override("normal_font", mono);
	log->add_theme_font_size_override("normal_font_size", font_size);
	log->add_theme_constant_override("line_separation", 2 * EDSCALE);

	Ref<StyleBoxFlat> log_style;
	log_style.instantiate();
	log_style->set_bg_color(get_theme_color(SNAME("dark_color_2"), EditorStringName(Editor)));
	log_style->set_corner_radius_all(6 * EDSCALE);
	log_style->set_content_margin_all(10 * EDSCALE);
	log_panel->add_theme_style_override(SceneStringName(panel), log_style);

	action_panel->add_theme_style_override(SceneStringName(panel), _make_card_style(accent, 0.05));
	accept_panel->add_theme_style_override(SceneStringName(panel), _make_card_style(warning, 0.14));
	install_button->set_button_icon(get_editor_theme_icon(SNAME("Load")));
	cancel_button->set_button_icon(get_editor_theme_icon(SNAME("Stop")));
	restart_button->set_button_icon(get_editor_theme_icon(SNAME("Reload")));
	open_folder_button->set_button_icon(get_editor_theme_icon(SNAME("Folder")));
	progress_bar->set_custom_minimum_size(Size2(0, 22 * EDSCALE));

	why_text->set_text(TTR("[b]DLSS[/b] is NVIDIA's AI upscaler. It renders fewer pixels and reconstructs a sharp image, so games run faster on NVIDIA RTX cards.") + "\n" +
			TTR("The files that make DLSS work come from NVIDIA. NVIDIA's license doesn't allow this editor's download to include them, so this window gets them for you from NVIDIA's own GitHub page: the same file you'd get by clicking the download link yourself.") + "\n" +
			"[color=" + success_html + "]" + TTR("[b]Optional.[/b] The editor, the path tracer and PhysX all work without it.") + "[/color]\n" +
			"[color=" + accent_html + "]" + TTR("[b]Nothing connects to the internet[/b] until you press \"Download and Install\" and confirm. Links ask before they open your browser.") + "[/color]");

	// Step list: number, what happens, and why.
	String steps = "[table=2]";
	int step_number = 0;
	auto add_step = [&](const String &p_title, const String &p_explain, const String &p_value) {
		step_number++;
		steps += "[cell padding=0,4,12,8][color=" + accent_html + "][b]" + itos(step_number) + "[/b][/color][/cell]";
		steps += "[cell expand=1 padding=0,4,0,8][b]" + p_title + "[/b]\n[color=" + dim + "]" + p_explain + "[/color]";
		if (!p_value.is_empty()) {
			steps += "\n" + p_value;
		}
		steps += "[/cell]";
	};
	add_step(TTR("Check the editor folder"), TTR("Makes sure the files can be written there, before anything is downloaded."), _code(_get_install_dir()));
	add_step(TTR("Ask you to confirm the download"), TTR("A popup shows exactly what will be downloaded and from where. Nothing happens if you say no."), "");
	add_step(vformat(TTR("Download the Streamline SDK %s (%s)"), SL_SDK_VERSION, String::humanize_size(SL_DOWNLOAD_SIZE)), TTR("A normal HTTPS download from NVIDIA's official GitHub account, NVIDIA-RTX. GitHub serves the file from its own download server. Nothing about you or your project is sent."), _code(SL_DOWNLOAD_URL));
	add_step(TTR("Keep it in the editor's cache folder for now"), TTR("A temporary copy, deleted at the end."), _code(_get_download_path()));
	add_step(TTR("Check the file's SHA-256 fingerprint"), TTR("A fingerprint of every byte in the file. This build knows the fingerprint of NVIDIA's official release. If even one byte differs (a broken or tampered download), the file is deleted and nothing is installed."), _code(SL_DOWNLOAD_SHA256));
	add_step(vformat(TTR("Copy %d files next to the editor"), SL_FILE_COUNT), vformat(TTR("Only the signed release files from the zip's \"%s\" folder (listed below). Documentation, source code and the \"development\" debug builds are skipped. A file that already exists is renamed to <name>.old, not deleted, so you can roll back."), SL_ZIP_DIR), "");
	add_step(TTR("Delete the downloaded zip"), TTR("Frees the 203 MiB cache copy."), "");
	add_step(TTR("Restart the editor"), TTR("Streamline is loaded when the editor starts. Then pick DLSS in Project Settings > Rendering > Scaling 3D > Mode."), "");
	steps += "[/table]";
	steps_text->set_text(steps);

	// What each installed file does.
	struct FileInfo {
		const char *files;
		String purpose;
	};
	const FileInfo file_info[] = {
		{ "sl.interposer.dll", TTR("Streamline loader. The editor looks for this file at startup.") },
		{ "sl.common.dll", TTR("Shared code used by all Streamline features.") },
		{ "sl.dlss.dll, nvngx_dlss.dll", TTR("DLSS Super Resolution (AI upscaling).") },
		{ "sl.dlss_d.dll, nvngx_dlssd.dll", TTR("DLSS Ray Reconstruction (AI denoiser for the path tracer).") },
		{ "sl.dlss_g.dll, nvngx_dlssg.dll", TTR("DLSS Frame Generation.") },
		{ "sl.reflex.dll, sl.pcl.dll, NvLowLatencyVk.dll", TTR("NVIDIA Reflex: lower input latency, plus latency statistics.") },
		{ "sl.nis.dll", TTR("NVIDIA Image Scaling, a simple upscaler for any GPU.") },
		{ "sl.deepdvc.dll, nvngx_deepdvc.dll", TTR("RTX Dynamic Vibrance (color enhancement).") },
		{ "sl.directsr.dll", TTR("Microsoft DirectSR support (Direct3D 12).") },
		{ "sl.nvperf.dll", TTR("Nsight Perf profiling support.") },
		{ "*.license.txt", TTR("License texts for DLSS, Reflex and NIS.") },
	};
	String files = "[table=2]";
	for (const FileInfo &info : file_info) {
		files += "[cell padding=0,2,16,6][code]" + String(info.files) + "[/code][/cell]";
		files += "[cell expand=1 padding=0,2,0,6][color=" + dim + "]" + info.purpose + "[/color][/cell]";
	}
	files += "[/table]";
	files_text->set_text(files);

	safety_text->set_text("[ul]" +
			TTR("Doesn't need or ask for administrator rights.") + "\n" +
			TTR("Doesn't run any installer or program. The files are only copied.") + "\n" +
			TTR("Doesn't touch the registry, system folders, drivers or PATH.") + "\n" +
			TTR("Doesn't change your projects.") + "\n" +
			TTR("Doesn't install a background service or send telemetry.") + "\n" +
			TTR("Doesn't connect anywhere except GitHub, and only after you confirm.") + "[/ul]\n" +
			"[color=" + dim + "]" + TTR("To undo it, delete the files listed above from the editor folder.") + "[/color]");

	manual_text->set_text(vformat(TTR("Download the zip yourself, open its \"%s\" folder, and copy the files listed above into the editor folder. Then restart the editor."), SL_ZIP_DIR) + "\n" +
			_link(vformat(TTR("Streamline SDK %s release page (GitHub)"), SL_SDK_VERSION), SL_RELEASE_URL) + "\n" +
			_link(TTR("Direct download of the zip"), SL_DOWNLOAD_URL) + "\n" +
			_link(TTR("NVIDIA Streamline product page"), SL_PRODUCT_URL) + "\n" +
			_link(TTR("Step-by-step instructions (README of this build)"), FORK_README_URL));

	license_text->set_text(TTR("The downloaded files are [b]NVIDIA software under NVIDIA's licenses[/b], not under Godot's MIT license.") + "\n" +
			"[ul]" + TTR("DLSS, Ray Reconstruction and Frame Generation: NVIDIA RTX SDKs License.") + "\n" +
			TTR("Reflex, NIS and Nsight Perf: their own NVIDIA licenses. The texts are installed next to the editor.") + "\n" +
			TTR("Among other things, they limit sharing the files with others and require you to be of legal age (or have a guardian's consent).") + "[/ul]\n" +
			"[color=" + warning_html + "]" + TTR("Read them before you continue:") + "[/color]\n" +
			_link(TTR("NVIDIA RTX SDKs License (DLSS)"), DLSS_LICENSE_URL) + "\n" +
			_link(TTR("Streamline SDK license"), SL_LICENSE_URL) + "\n" +
			_link(TTR("Streamline third-party licenses"), SL_THIRD_PARTY_URL));

	download_confirm_text->set_text(TTR("You are about to [b]download a file from GitHub[/b].") + "\n\n" +
			"[table=2]" +
			"[cell padding=0,2,12,6][b]" + TTR("From") + "[/b][/cell][cell expand=1 padding=0,2,0,6]" + TTR("NVIDIA's official GitHub account [b]NVIDIA-RTX[/b], Streamline repository") + "[/cell]" +
			"[cell padding=0,2,12,6][b]" + TTR("File") + "[/b][/cell][cell expand=1 padding=0,2,0,6]" + vformat("%s (%s)", SL_DOWNLOAD_FILE, String::humanize_size(SL_DOWNLOAD_SIZE)) + "[/cell]" +
			"[cell padding=0,2,12,6][b]" + TTR("Address") + "[/b][/cell][cell expand=1 padding=0,2,0,6]" + _code(SL_DOWNLOAD_URL) + "[/cell]" +
			"[cell padding=0,2,12,6][b]" + TTR("Saved to") + "[/b][/cell][cell expand=1 padding=0,2,0,6]" + _code(_get_download_path()) + "[/cell]" +
			"[/table]\n" +
			"[color=" + dim + "]" + TTR("GitHub serves the file from its download server (githubusercontent.com). Before anything is installed, the file is checked against the SHA-256 fingerprint of NVIDIA's official release.") + "[/color]\n\n" +
			TTR("Do you want to connect to GitHub and download it?"));

	_update_status();
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
	const bool installed = _is_installed();
	const Color color = get_theme_color(installed ? SNAME("success_color") : SNAME("warning_color"), EditorStringName(Editor));
	status_panel->add_theme_style_override(SceneStringName(panel), _make_card_style(color, 0.16));
	status_icon->set_texture(get_editor_theme_icon(installed ? SNAME("StatusSuccess") : SNAME("StatusWarning")));
	if (installed) {
		status_label->set_text(vformat(TTR("DLSS is installed. Its files are in %s. You can install again to replace them with Streamline SDK %s."), _get_install_dir(), SL_SDK_VERSION));
	} else {
		status_label->set_text(vformat(TTR("DLSS is not installed yet. Its files aren't in %s. DLSS, Ray Reconstruction, Frame Generation and Reflex are off until you add them; everything else works."), _get_install_dir()));
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
	_log(TTR("Asking for confirmation before connecting to GitHub..."));
	// Shrink to the text: the window keeps its largest size otherwise.
	download_confirm->reset_size();
	download_confirm->popup_centered();
}

void StreamlineInstaller::_start_install() {
	ERR_FAIL_COND(!accept_check->is_pressed());
	_log(TTR("Download confirmed."));

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
		case NOTIFICATION_THEME_CHANGED: {
			_update_theme();
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
	popup_centered_clamped(Size2(940, 860) * EDSCALE, 0.9);
}

StreamlineInstaller::StreamlineInstaller() {
	set_title(TTR("Get NVIDIA DLSS (Streamline SDK)"));
	get_ok_button()->set_text(TTR("Close"));

	VBoxContainer *main_vb = memnew(VBoxContainer);
	main_vb->add_theme_constant_override("separation", 10 * EDSCALE);
	add_child(main_vb);

	ScrollContainer *scroll = memnew(ScrollContainer);
	scroll->set_horizontal_scroll_mode(ScrollContainer::SCROLL_MODE_DISABLED);
	scroll->set_v_size_flags(Control::SIZE_EXPAND_FILL);
	main_vb->add_child(scroll);

	MarginContainer *scroll_margin = memnew(MarginContainer);
	scroll_margin->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	scroll_margin->add_theme_constant_override("margin_right", 8 * EDSCALE);
	scroll->add_child(scroll_margin);

	VBoxContainer *cards_vb = memnew(VBoxContainer);
	cards_vb->add_theme_constant_override("separation", 12 * EDSCALE);
	scroll_margin->add_child(cards_vb);

	// Status: installed or not, at a glance.
	status_panel = memnew(PanelContainer);
	cards_vb->add_child(status_panel);
	HBoxContainer *status_hb = memnew(HBoxContainer);
	status_hb->add_theme_constant_override("separation", 10 * EDSCALE);
	status_panel->add_child(status_hb);
	status_icon = memnew(TextureRect);
	status_icon->set_stretch_mode(TextureRect::STRETCH_KEEP_CENTERED);
	status_hb->add_child(status_icon);
	status_label = memnew(Label);
	status_label->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	status_label->set_h_size_flags(Control::SIZE_EXPAND_FILL);
	status_hb->add_child(status_label);

	VBoxContainer *vb = _add_card(cards_vb, TTR("What is this?"), SNAME("NodeInfo"), SNAME("accent_color"));
	why_text = _add_text(vb);

	vb = _add_card(cards_vb, TTR("What \"Download and Install\" does, step by step"), SNAME("Load"), SNAME("accent_color"));
	steps_text = _add_text(vb);

	vb = _add_card(cards_vb, TTR("Files that get installed, and what they do"), SNAME("File"), SNAME("accent_color"));
	files_text = _add_text(vb);

	vb = _add_card(cards_vb, TTR("What this does NOT do"), SNAME("Lock"), SNAME("success_color"));
	safety_text = _add_text(vb);

	vb = _add_card(cards_vb, TTR("Prefer to do it yourself?"), SNAME("ExternalLink"), SNAME("accent_color"));
	manual_text = _add_text(vb);
	open_folder_button = memnew(Button(TTR("Open Editor Folder")));
	open_folder_button->set_h_size_flags(Control::SIZE_SHRINK_BEGIN);
	open_folder_button->connect(SceneStringName(pressed), callable_mp(this, &StreamlineInstaller::_open_folder_pressed));
	vb->add_child(open_folder_button);

	// License terms. Installing stays disabled until the user accepts.
	vb = _add_card(cards_vb, TTR("NVIDIA license terms"), SNAME("StatusWarning"), SNAME("warning_color"));
	license_text = _add_text(vb);
	accept_panel = memnew(PanelContainer);
	vb->add_child(accept_panel);
	accept_check = memnew(CheckBox(TTR("I have read and accept NVIDIA's license terms for the Streamline SDK, DLSS, Reflex and NIS.")));
	accept_check->set_autowrap_mode(TextServer::AUTOWRAP_WORD_SMART);
	accept_check->connect(SceneStringName(toggled), callable_mp(this, &StreamlineInstaller::_accept_toggled));
	accept_panel->add_child(accept_check);

	// Controls, progress and log stay visible below the scrolled cards.
	action_panel = memnew(PanelContainer);
	main_vb->add_child(action_panel);
	VBoxContainer *action_vb = memnew(VBoxContainer);
	action_vb->add_theme_constant_override("separation", 8 * EDSCALE);
	action_panel->add_child(action_vb);

	HBoxContainer *hb = memnew(HBoxContainer);
	hb->add_theme_constant_override("separation", 8 * EDSCALE);
	action_vb->add_child(hb);
	install_button = memnew(Button);
	install_button->set_custom_minimum_size(Size2(200, 0) * EDSCALE);
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
	action_vb->add_child(progress_bar);

	Label *log_title = memnew(Label(TTR("Activity log (every step is listed here)")));
	log_title->set_theme_type_variation("HeaderSmall");
	action_vb->add_child(log_title);
	log_panel = memnew(PanelContainer);
	action_vb->add_child(log_panel);
	log = memnew(RichTextLabel);
	log->set_custom_minimum_size(Size2(0, 150) * EDSCALE);
	log->set_scroll_follow(true);
	log->set_selection_enabled(true);
	log->set_context_menu_enabled(true);
	log_panel->add_child(log);

	// The only place a network connection can start: the user confirms this popup.
	download_confirm = memnew(ConfirmationDialog);
	download_confirm->set_title(TTR("Download from GitHub?"));
	download_confirm->set_ok_button_text(TTR("Yes, download from GitHub"));
	download_confirm->set_cancel_button_text(TTR("No, don't connect"));
	download_confirm_text = memnew(RichTextLabel);
	download_confirm_text->set_use_bbcode(true);
	download_confirm_text->set_fit_content(true);
	// A fixed width lets fit_content measure the real height. Without it the
	// text wraps at a tiny width and the popup becomes very tall.
	download_confirm_text->set_custom_minimum_size(Size2(600, 0) * EDSCALE);
	download_confirm_text->set_scroll_active(false);
	download_confirm_text->set_selection_enabled(true);
	download_confirm->add_child(download_confirm_text);
	download_confirm->connect(SceneStringName(confirmed), callable_mp(this, &StreamlineInstaller::_start_install));
	download_confirm->connect("canceled", callable_mp(this, &StreamlineInstaller::_log).bind(TTR("Download declined. Nothing was downloaded."), Color()));
	add_child(download_confirm);

	link_confirm = memnew(ConfirmationDialog);
	link_confirm->set_title(TTR("Open Link?"));
	link_confirm->set_ok_button_text(TTR("Open in Browser"));
	link_confirm->set_cancel_button_text(TTR("Don't Open"));
	link_confirm->set_autowrap(true);
	link_confirm->set_min_size(Size2(520, 0) * EDSCALE);
	link_confirm->connect(SceneStringName(confirmed), callable_mp(this, &StreamlineInstaller::_open_pending_link));
	add_child(link_confirm);
	texts.push_back(download_confirm_text);

	downloader = memnew(HTTPRequest);
	downloader->set_use_threads(true);
	downloader->connect("request_completed", callable_mp(this, &StreamlineInstaller::_download_completed));
	add_child(downloader);
}
