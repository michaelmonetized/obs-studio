/******************************************************************************
    Copyright (C) 2023 by Lain Bailey <lain@obsproject.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
******************************************************************************/

#include "OBSBasic.hpp"

#include <dialogs/OBSStreamDestinationDialog.hpp>

#include <nlohmann/json.hpp>
#include <qt-wrappers.hpp>

constexpr std::string_view OBSExtraServicesFileName = "extra_services.json";

void OBSBasic::RebuildExtraStreamServices()
{
	extraStreamServices.clear();
	extraStreamServices.reserve(extraStreamDestinations.size());

	for (size_t i = 0; i < extraStreamDestinations.size(); i++) {
		const auto &entry = extraStreamDestinations[i];
		std::string serviceName = "extra_service_" + std::to_string(i);
		OBSService service =
			obs_service_create(entry.type.c_str(), serviceName.c_str(), entry.settings, nullptr);
		if (service) {
			extraStreamServices.push_back(service);
		}
	}
}

bool OBSBasic::LoadExtraStreamDestinations()
{
	extraStreamDestinations.clear();
	extraStreamServices.clear();

	try {
		const OBSProfile &currentProfile = GetCurrentProfile();
		const std::filesystem::path jsonFilePath =
			currentProfile.path / std::filesystem::u8path(OBSExtraServicesFileName);

		if (!std::filesystem::exists(jsonFilePath)) {
			return true;
		}

		OBSDataAutoRelease data = obs_data_create_from_json_file_safe(jsonFilePath.u8string().c_str(), "bak");
		if (!data) {
			return false;
		}

		obs_data_item_t *item = obs_data_first(data);
		for (; item != nullptr; obs_data_item_next(&item)) {
			if (obs_data_item_gettype(item) != OBS_DATA_OBJECT) {
				continue;
			}

			const char *name = obs_data_item_get_name(item);
			OBSDataAutoRelease entry = obs_data_item_get_obj(item);
			if (!entry) {
				continue;
			}

			ExtraStreamDestinationData destination;
			destination.displayName = obs_data_get_string(entry, "name");
			destination.type = obs_data_get_string(entry, "type");
			destination.settings = obs_data_get_obj(entry, "settings");

			if (destination.type.empty() || !destination.settings) {
				continue;
			}

			if (destination.displayName.empty()) {
				destination.displayName = name;
			}

			extraStreamDestinations.push_back(std::move(destination));
		}
	} catch (const std::invalid_argument &error) {
		blog(LOG_ERROR, "%s", error.what());
		return false;
	}

	RebuildExtraStreamServices();
	QMetaObject::invokeMethod(this, "UpdateStreamDestinationsStatus", Qt::QueuedConnection);
	return true;
}

void OBSBasic::SaveExtraStreamDestinations()
{
	try {
		const OBSProfile &currentProfile = GetCurrentProfile();
		const std::filesystem::path jsonFilePath =
			currentProfile.path / std::filesystem::u8path(OBSExtraServicesFileName);

		OBSDataAutoRelease data = obs_data_create();

		for (size_t i = 0; i < extraStreamDestinations.size(); i++) {
			const auto &destination = extraStreamDestinations[i];
			OBSDataAutoRelease entry = obs_data_create();
			obs_data_set_string(entry, "name", destination.displayName.c_str());
			obs_data_set_string(entry, "type", destination.type.c_str());
			obs_data_set_obj(entry, "settings", destination.settings);

			std::string key = "destination_" + std::to_string(i);
			obs_data_set_obj(data, key.c_str(), entry);
		}

		if (!obs_data_save_json_safe(data, jsonFilePath.u8string().c_str(), "tmp", "bak")) {
			blog(LOG_WARNING, "Failed to save extra stream destinations");
		}
	} catch (const std::invalid_argument &error) {
		blog(LOG_ERROR, "%s", error.what());
	}
}

std::vector<std::pair<OBSService, std::string>> OBSBasic::GetExtraStreamDestinations() const
{
	std::vector<std::pair<OBSService, std::string>> destinations;
	destinations.reserve(extraStreamServices.size());

	for (size_t i = 0; i < extraStreamServices.size(); i++) {
		if (!extraStreamServices[i]) {
			continue;
		}

		std::string name = i < extraStreamDestinations.size() ? extraStreamDestinations[i].displayName
								      : "Destination";
		destinations.emplace_back(extraStreamServices[i], name);
	}

	return destinations;
}

bool OBSBasic::AddExtraStreamDestination(const char *type, obs_data_t *settings, const std::string &displayName)
{
	if (!type || !settings) {
		return false;
	}

	ExtraStreamDestinationData destination;
	destination.displayName = displayName;
	destination.type = type;
	OBSDataAutoRelease copy = obs_data_create();
	obs_data_apply(copy, settings);
	destination.settings = copy;

	extraStreamDestinations.push_back(std::move(destination));
	RebuildExtraStreamServices();
	SaveExtraStreamDestinations();
	return true;
}

void OBSBasic::RemoveExtraStreamDestination(size_t index)
{
	if (index >= extraStreamDestinations.size()) {
		return;
	}

	extraStreamDestinations.erase(extraStreamDestinations.begin() + static_cast<ptrdiff_t>(index));
	RebuildExtraStreamServices();
	SaveExtraStreamDestinations();
}

void OBSBasic::ShowAddStreamDestinationDialog(QWidget *parent)
{
	if (!parent) {
		parent = this;
	}

	OBSStreamDestinationResult result;
	if (!OBSStreamDestinationDialog::GetDestination(parent, result)) {
		return;
	}

	if (!AddExtraStreamDestination(result.type.c_str(), result.settings, result.displayName)) {
		return;
	}

	if (outputHandler) {
		outputHandler->extraStreamOutputs.Clear();
	}

	UpdateStreamDestinationsStatus();
}
