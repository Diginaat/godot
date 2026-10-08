/**************************************************************************/
/*  streamline_installer.h                                                */
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

#pragma once

#include "scene/gui/dialogs.h"

class Button;
class CheckBox;
class HTTPRequest;
class Label;
class PanelContainer;
class ProgressBar;
class RichTextLabel;
class TextureRect;
class VBoxContainer;

// Downloads the NVIDIA Streamline SDK release from GitHub and installs its
// runtime DLLs next to the editor executable, so DLSS can be used. NVIDIA's
// license doesn't allow bundling these files with the editor.
class StreamlineInstaller : public AcceptDialog {
	GDCLASS(StreamlineInstaller, AcceptDialog);

	enum State {
		STATE_IDLE,
		STATE_DOWNLOADING,
		STATE_VERIFYING,
		STATE_EXTRACTING,
		STATE_DONE,
		STATE_FAILED,
		STATE_CANCELED,
	};

	State state = STATE_IDLE;
	int extract_index = 0;
	int installed_count = 0;
	uint64_t download_start_msec = 0;

	// A titled panel with a colored left edge. Colors and icons come from the
	// editor theme, so they're applied in _update_theme().
	struct Card {
		PanelContainer *panel = nullptr;
		TextureRect *icon = nullptr;
		Label *title = nullptr;
		StringName icon_name;
		StringName color_name;
	};
	Vector<Card> cards;
	Vector<RichTextLabel *> texts;

	PanelContainer *status_panel = nullptr;
	TextureRect *status_icon = nullptr;
	Label *status_label = nullptr;
	RichTextLabel *why_text = nullptr;
	RichTextLabel *steps_text = nullptr;
	RichTextLabel *files_text = nullptr;
	RichTextLabel *safety_text = nullptr;
	RichTextLabel *manual_text = nullptr;
	RichTextLabel *license_text = nullptr;
	PanelContainer *accept_panel = nullptr;
	CheckBox *accept_check = nullptr;
	Button *open_folder_button = nullptr;

	PanelContainer *action_panel = nullptr;
	Button *install_button = nullptr;
	Button *cancel_button = nullptr;
	Button *restart_button = nullptr;
	ProgressBar *progress_bar = nullptr;
	Label *progress_label = nullptr;
	PanelContainer *log_panel = nullptr;
	RichTextLabel *log = nullptr;
	HTTPRequest *downloader = nullptr;
	ConfirmationDialog *download_confirm = nullptr;
	RichTextLabel *download_confirm_text = nullptr;
	ConfirmationDialog *restart_confirm = nullptr;
	ConfirmationDialog *link_confirm = nullptr;
	String pending_url;

	VBoxContainer *_add_card(VBoxContainer *p_parent, const String &p_title, const StringName &p_icon, const StringName &p_color);
	RichTextLabel *_add_text(VBoxContainer *p_parent);
	Ref<StyleBox> _make_card_style(const Color &p_edge, float p_tint) const;
	String _code(const String &p_text) const;
	String _link(const String &p_label, const String &p_url) const;
	void _update_theme();
	void _meta_clicked(const Variant &p_meta);
	void _open_pending_link();

	String _get_install_dir() const;
	String _get_download_path() const;
	bool _is_installed() const;
	void _update_status();
	void _update_buttons();
	void _log(const String &p_text, const Color &p_color = Color());
	void _fail(const String &p_text);

	void _accept_toggled(bool p_pressed);
	void _install_pressed();
	void _start_install();
	void _cancel_pressed();
	void _restart_pressed();
	void _open_folder_pressed();

	void _download_completed(int p_result, int p_response_code, const PackedStringArray &p_headers, const PackedByteArray &p_body);
	void _verify();
	void _extract_next();
	bool _extract_file(const String &p_name);

protected:
	void _notification(int p_what);

public:
	void popup_installer();

	StreamlineInstaller();
};
