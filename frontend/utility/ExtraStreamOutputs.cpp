#include "ExtraStreamOutputs.hpp"
#include "BasicOutputHandler.hpp"

#include <widgets/OBSBasic.hpp>

#include <util/base.h>

void ExtraStreamOutputs::Clear()
{
	outputs_.clear();
	main_ = nullptr;
}

void ExtraStreamOutputs::NotifyStatusChanged()
{
	if (!main_) {
		return;
	}

	QMetaObject::invokeMethod(main_, "UpdateStreamDestinationsStatus", Qt::QueuedConnection);
}

void ExtraStreamOutputs::SetStatus(obs_output_t *output, ExtraDestinationStatus status)
{
	for (auto &entry : outputs_) {
		if (entry.output.Get() == output) {
			entry.status = status;
			NotifyStatusChanged();
			return;
		}
	}
}

void ExtraStreamOutputs::OBSExtraStreamReconnect(void *data, calldata_t *params)
{
	auto *self = static_cast<ExtraStreamOutputs *>(data);
	obs_output_t *output = (obs_output_t *)calldata_ptr(params, "output");
	self->SetStatus(output, ExtraDestinationStatus::DestReconnecting);
}

void ExtraStreamOutputs::OBSExtraStreamReconnectSuccess(void *data, calldata_t *params)
{
	auto *self = static_cast<ExtraStreamOutputs *>(data);
	obs_output_t *output = (obs_output_t *)calldata_ptr(params, "output");
	self->SetStatus(output, ExtraDestinationStatus::DestLive);
}

void ExtraStreamOutputs::OBSExtraStreamStopped(void *data, calldata_t *params)
{
	auto *self = static_cast<ExtraStreamOutputs *>(data);
	obs_output_t *output = (obs_output_t *)calldata_ptr(params, "output");
	const int code = (int)calldata_int(params, "code");

	if (code == OBS_OUTPUT_SUCCESS) {
		self->SetStatus(output, ExtraDestinationStatus::DestIdle);
	} else {
		self->SetStatus(output, ExtraDestinationStatus::DestError);
	}
}

void ExtraStreamOutputs::ConnectSignals(ExtraStreamOutput &entry)
{
	signal_handler_t *handler = obs_output_get_signal_handler(entry.output);
	entry.outputSignals.emplace_back(handler, "reconnect", OBSExtraStreamReconnect, this);
	entry.outputSignals.emplace_back(handler, "reconnect_success", OBSExtraStreamReconnectSuccess, this);
	entry.outputSignals.emplace_back(handler, "stop", OBSExtraStreamStopped, this);
}

bool ExtraStreamOutputs::Setup(OBSBasic *main, const std::vector<std::pair<OBSService, std::string>> &destinations,
			       obs_encoder_t *videoEncoder, obs_encoder_t *audioEncoder)
{
	Clear();
	main_ = main;

	if (!videoEncoder || !audioEncoder) {
		return false;
	}

	outputs_.reserve(destinations.size());

	for (size_t i = 0; i < destinations.size(); i++) {
		obs_service_t *service = destinations[i].first;
		if (!service) {
			continue;
		}

		const char *type = GetStreamOutputType(service);
		if (!type) {
			blog(LOG_WARNING, "Failed to determine output type for extra stream destination '%s'",
			     destinations[i].second.c_str());
			return false;
		}

		std::string outputName = "extra_stream_" + std::to_string(i);
		OBSOutputAutoRelease output = obs_output_create(type, outputName.c_str(), nullptr, nullptr);
		if (!output) {
			blog(LOG_WARNING, "Failed to create extra stream output '%s'", outputName.c_str());
			return false;
		}

		obs_output_set_video_encoder(output, videoEncoder);
		obs_output_set_audio_encoder(output, audioEncoder, 0);
		obs_output_set_service(output, service);

		ExtraStreamOutput entry;
		entry.output = std::move(output);
		entry.service = destinations[i].first;
		entry.displayName = destinations[i].second;
		entry.status = ExtraDestinationStatus::DestIdle;
		ConnectSignals(entry);
		outputs_.push_back(std::move(entry));
	}

	return true;
}

bool ExtraStreamOutputs::Start(obs_data_t *outputSettings, int delaySec, bool preserveDelay, int maxRetries,
			       int retryDelay)
{
	for (auto &entry : outputs_) {
		obs_output_update(entry.output, outputSettings);
		obs_output_set_delay(entry.output, delaySec, preserveDelay ? OBS_OUTPUT_DELAY_PRESERVE : 0);
		obs_output_set_reconnect_settings(entry.output, maxRetries, retryDelay);
		obs_output_set_service(entry.output, entry.service);

		if (!obs_output_start(entry.output)) {
			const char *error = obs_output_get_last_error(entry.output);
			entry.status = ExtraDestinationStatus::DestError;
			blog(LOG_WARNING, "Extra stream destination '%s' failed to start%s%s", entry.displayName.c_str(),
			     error && *error ? ": " : "", error && *error ? error : "");
			NotifyStatusChanged();
			Stop(true);
			return false;
		}

		entry.status = ExtraDestinationStatus::DestLive;
		blog(LOG_INFO, "Started extra stream destination: %s", entry.displayName.c_str());
	}

	NotifyStatusChanged();
	return true;
}

void ExtraStreamOutputs::Stop(bool force)
{
	for (auto &entry : outputs_) {
		if (!entry.output) {
			continue;
		}

		if (!obs_output_active(entry.output)) {
			entry.status = ExtraDestinationStatus::DestIdle;
			continue;
		}

		if (force) {
			obs_output_force_stop(entry.output);
		} else {
			obs_output_stop(entry.output);
		}

		entry.status = ExtraDestinationStatus::DestIdle;
	}

	NotifyStatusChanged();
}

bool ExtraStreamOutputs::AnyActive() const
{
	for (const auto &entry : outputs_) {
		if (entry.output && obs_output_active(entry.output)) {
			return true;
		}
	}
	return false;
}
