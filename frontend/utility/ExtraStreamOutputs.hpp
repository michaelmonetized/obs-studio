#pragma once

#include <obs.hpp>

#include <string>
#include <utility>
#include <vector>

class OBSBasic;

enum class ExtraDestinationStatus { DestIdle, DestLive, DestReconnecting, DestError };

struct ExtraStreamOutput {
	OBSOutputAutoRelease output;
	OBSService service;
	std::string displayName;
	ExtraDestinationStatus status = ExtraDestinationStatus::DestIdle;
	std::vector<OBSSignal> outputSignals;
};

class ExtraStreamOutputs {
public:
	void Clear();

	bool Setup(OBSBasic *main, const std::vector<std::pair<OBSService, std::string>> &destinations,
		   obs_encoder_t *videoEncoder, obs_encoder_t *audioEncoder);

	bool Start(obs_data_t *outputSettings, int delaySec, bool preserveDelay, int maxRetries, int retryDelay);

	void Stop(bool force = false);

	bool AnyActive() const;

	const std::vector<ExtraStreamOutput> &Entries() const { return outputs_; }

	static void OBSExtraStreamReconnect(void *data, calldata_t *params);
	static void OBSExtraStreamReconnectSuccess(void *data, calldata_t *params);
	static void OBSExtraStreamStopped(void *data, calldata_t *params);

private:
	OBSBasic *main_ = nullptr;
	std::vector<ExtraStreamOutput> outputs_;

	void SetStatus(obs_output_t *output, ExtraDestinationStatus status);
	void ConnectSignals(ExtraStreamOutput &entry);
	void NotifyStatusChanged();
};
