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

#include <utility/ExtraStreamOutputs.hpp>

#include <qt-wrappers.hpp>

static QString DestinationStatusText(ExtraDestinationStatus status)
{
	switch (status) {
	case ExtraDestinationStatus::DestLive:
		return QTStr("Basic.StreamDestination.Status.Live");
	case ExtraDestinationStatus::DestReconnecting:
		return QTStr("Basic.StreamDestination.Status.Reconnecting");
	case ExtraDestinationStatus::DestError:
		return QTStr("Basic.StreamDestination.Status.Error");
	case ExtraDestinationStatus::DestIdle:
	default:
		return QTStr("Basic.StreamDestination.Status.Inactive");
	}
}

static QString PrimaryDestinationStatusText(const OBSBasic *main)
{
	if (!main->StreamingActive()) {
		return QTStr("Basic.StreamDestination.Status.Inactive");
	}

	return QTStr("Basic.StreamDestination.Status.Live");
}

QString OBSBasic::GetStreamDestinationLabel(obs_service_t *service) const
{
	if (!service) {
		return QTStr("Basic.StreamDestination.Primary");
	}

	OBSDataAutoRelease settings = obs_service_get_settings(service);
	const char *type = obs_service_get_type(service);

	if (strcmp(type, "rtmp_custom") == 0 || strcmp(type, "whip_custom") == 0) {
		const char *server = obs_data_get_string(settings, "server");
		if (server && *server) {
			return QString(server);
		}
	}

	const char *serviceName = obs_data_get_string(settings, "service");
	if (serviceName && *serviceName) {
		return QString(serviceName);
	}

	return QTStr("Basic.StreamDestination.Primary");
}

QString OBSBasic::BuildStreamDestinationsStatusText() const
{
	QStringList parts;

	const QString primaryLabel = GetStreamDestinationDisplayName();
	parts << QString("%1: %2").arg(primaryLabel, PrimaryDestinationStatusText(this));

	for (const auto &entry : GetExtraStreamOutputEntries()) {
		parts << QString("%1: %2").arg(QString::fromStdString(entry.displayName),
					       DestinationStatusText(entry.status));
	}

	return parts.join(" | ");
}

QString OBSBasic::GetStreamDestinationDisplayName() const
{
	const QString label = GetStreamDestinationLabel(GetService());
	if (!label.isEmpty()) {
		return label;
	}

	return QTStr("Basic.StreamDestination.Primary");
}

bool OBSBasic::StreamDestinationsRequireOpus() const
{
	auto check_service = [](obs_service_t *service) {
		return service && astrcmpi(obs_service_get_protocol(service), "WHIP") == 0;
	};

	if (check_service(GetService())) {
		return true;
	}

	for (const auto &service : extraStreamServices) {
		if (check_service(service)) {
			return true;
		}
	}

	return false;
}

bool OBSBasic::ValidateStreamDestinationEncoders(QString &error) const
{
	if (!StreamDestinationsRequireOpus()) {
		return true;
	}

	const char *mode = config_get_string(activeConfiguration, "Output", "Mode");
	const char *audioEncoder = strcmp(mode, "Advanced") == 0
					   ? config_get_string(activeConfiguration, "AdvOut", "AudioEncoder")
					   : config_get_string(activeConfiguration, "SimpleOutput", "StreamAudioEncoder");

	const char *codec = audioEncoder ? obs_get_encoder_codec(audioEncoder) : nullptr;
	if (codec && strcmp(codec, "opus") == 0) {
		return true;
	}

	error = QTStr("Basic.StreamDestination.WHIPRequiresOpus");
	return false;
}

void OBSBasic::UpdateStreamDestinationsStatus()
{
	if (extraStreamDestinations.empty()) {
		emit StreamDestinationsStatusChanged(QString());
		return;
	}

	const QString summary = BuildStreamDestinationsStatusText();
	emit StreamDestinationsStatusChanged(summary);

	if (StreamingActive()) {
		ShowStatusBarMessage(summary);
	}
}

const std::vector<ExtraStreamOutput> &OBSBasic::GetExtraStreamOutputEntries() const
{
	static const std::vector<ExtraStreamOutput> empty;
	if (!outputHandler) {
		return empty;
	}
	return outputHandler->extraStreamOutputs.Entries();
}
