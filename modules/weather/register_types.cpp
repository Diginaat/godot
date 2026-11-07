/**************************************************************************/
/*  register_types.cpp                                                    */
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

#include "register_types.h"

#include "effects/weather_haze_effect.h"
#include "effects/weather_lighting_effect.h"
#include "effects/weather_lightning_effect.h"
#include "effects/weather_motes_effect.h"
#include "effects/weather_rain_effect.h"
#include "effects/weather_sky_effect.h"
#include "weather.h"
#include "weather_effect.h"
#include "weather_library.h"
#include "weather_profile.h"
#include "wind/wind_receiver_3d.h"
#include "wind/wind_server.h"

#include "core/config/engine.h"
#include "core/object/class_db.h"

#ifdef TOOLS_ENABLED
#include "editor/editor_node.h"
#include "editor/export/editor_export.h"
#include "editor/weather_export_plugin.h"

static void _weather_editor_init() {
	Ref<WeatherExportPlugin> export_plugin;
	export_plugin.instantiate();
	EditorExport::get_singleton()->add_export_plugin(export_plugin);
}
#endif

static WindServer *wind_server = nullptr;

void initialize_weather_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_CORE) {
		WindServer::register_project_settings();
		Weather::register_project_settings();
	}

	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		GDREGISTER_CLASS(WindServer);
		GDREGISTER_CLASS(WindReceiver3D);
		GDREGISTER_CLASS(WeatherProfile);
		GDREGISTER_CLASS(WeatherLibrary);
		GDREGISTER_CLASS(WeatherEffect);
		GDREGISTER_CLASS(WeatherLightingEffect);
		GDREGISTER_CLASS(WeatherSkyEffect);
		GDREGISTER_CLASS(WeatherRainEffect);
		GDREGISTER_CLASS(WeatherHazeEffect);
		GDREGISTER_CLASS(WeatherMotesEffect);
		GDREGISTER_CLASS(WeatherLightningEffect);
		GDREGISTER_CLASS(Weather);

		// Created at the scene level, after the RenderingServer: it registers
		// the wind shader globals before any scene or shader loads.
		wind_server = memnew(WindServer);
		wind_server->initialize();
		Engine::get_singleton()->add_singleton(Engine::Singleton("WindServer", WindServer::get_singleton()));
		Weather::register_shader_globals();
		Weather::register_builtin_resources();
	}

#ifdef TOOLS_ENABLED
	if (p_level == MODULE_INITIALIZATION_LEVEL_EDITOR) {
		EditorNode::add_init_callback(_weather_editor_init);
	}
#endif
}

void uninitialize_weather_module(ModuleInitializationLevel p_level) {
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE) {
		WeatherLibrary::free_default();
		Weather::free_builtin_resources();
	}
	if (p_level == MODULE_INITIALIZATION_LEVEL_SCENE && wind_server) {
		wind_server->finalize();
		memdelete(wind_server);
		wind_server = nullptr;
	}
}
