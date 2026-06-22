#include "OBSStreamDestinationDialog.hpp"

#include <OBSApp.hpp>
#include <oauth/OAuth.hpp>
#include <utility/OBSEventFilter.hpp>

#include <qt-wrappers.hpp>

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QStackedWidget>
#include <QVBoxLayout>

struct QCef;
extern QCef *cef;

#include "moc_OBSStreamDestinationDialog.cpp"

OBSStreamDestinationDialog::OBSStreamDestinationDialog(QWidget *parent) : QDialog(parent)
{
	installEventFilter(CreateShortcutFilter());
	setModal(true);
	setWindowModality(Qt::WindowModality::WindowModal);
	setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
	setWindowTitle(QTStr("Basic.Settings.Stream.AddDestination"));
	setMinimumWidth(500);

	auto *layout = new QVBoxLayout(this);

	auto *destinationGroup = new QGroupBox(QTStr("Basic.Settings.Stream.Destination"), this);
	auto *outerLayout = new QVBoxLayout(destinationGroup);

	serviceCombo = new QComboBox(destinationGroup);
	outerLayout->addWidget(serviceCombo);

	authStack = new QStackedWidget(destinationGroup);

	auto *connectPage = new QWidget();
	auto *connectLayout = new QVBoxLayout(connectPage);
	connectAccountButton = new QPushButton(QTStr("Basic.AutoConfig.StreamPage.ConnectAccount"), connectPage);
	useStreamKeyButton = new QPushButton(QTStr("Basic.AutoConfig.StreamPage.UseStreamKey"), connectPage);
	connectLayout->addWidget(connectAccountButton);
	connectLayout->addWidget(useStreamKeyButton);
	connectLayout->addStretch();
	authStack->addWidget(connectPage);

	auto *destinationPage = new QWidget();
	auto *form = new QFormLayout(destinationPage);

	serverStack = new QStackedWidget(destinationPage);
	serverCombo = new QComboBox();
	customServerEdit = new QLineEdit();
	serverStack->addWidget(serverCombo);
	serverStack->addWidget(customServerEdit);
	form->addRow(QTStr("Basic.AutoConfig.StreamPage.Server"), serverStack);

	keyLabel = new QLabel(QTStr("Basic.AutoConfig.StreamPage.StreamKey"), destinationPage);
	keyEdit = new QLineEdit(destinationPage);
	keyEdit->setEchoMode(QLineEdit::Password);
	form->addRow(keyLabel, keyEdit);

	nameEdit = new QLineEdit(destinationPage);
	form->addRow(QTStr("Basic.Settings.Stream.DestinationName"), nameEdit);

	useAuthCheck = new QCheckBox(QTStr("Basic.Settings.Stream.Custom.UseAuthentication"), destinationPage);
	form->addRow(QString(), useAuthCheck);

	authUsernameEdit = new QLineEdit(destinationPage);
	form->addRow(QTStr("Basic.Settings.Stream.Custom.Username"), authUsernameEdit);

	authPwEdit = new QLineEdit(destinationPage);
	authPwEdit->setEchoMode(QLineEdit::Password);
	form->addRow(QTStr("Basic.Settings.Stream.Custom.Password"), authPwEdit);

	authStack->addWidget(destinationPage);
	outerLayout->addWidget(authStack);

	layout->addWidget(destinationGroup);

	auto *buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
	layout->addWidget(buttonBox);

	connect(buttonBox, &QDialogButtonBox::accepted, this, [this]() {
		if (Validate()) {
			accept();
		}
	});
	connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

	connect(serviceCombo, &QComboBox::currentIndexChanged, this, [this](int) {
		if (serviceCombo->currentData().toInt() == (int)ListOpt::ShowAll) {
			LoadServices(true);
			serviceCombo->showPopup();
			return;
		}
		auth.reset();
		authStack->setCurrentIndex((int)AuthPage::Connect);
		UpdateServerList();
		UpdateFieldVisibility();
	});
	connect(customServerEdit, &QLineEdit::textChanged, this, [this](const QString &) { UpdateFieldVisibility(); });
	connect(useAuthCheck, &QCheckBox::toggled, this, [this](bool checked) {
		authUsernameEdit->setEnabled(checked);
		authPwEdit->setEnabled(checked);
	});
	connect(connectAccountButton, &QPushButton::clicked, this, [this]() {
		std::string service = serviceCombo->currentText().toStdString();
		OAuth::DeleteCookies(service);
		auth = OAuth::Login(this, service);
		if (auth) {
			OnAuthConnected();
		}
	});
	connect(useStreamKeyButton, &QPushButton::clicked, this, [this]() {
		authStack->setCurrentIndex((int)AuthPage::Destination);
	});

	LoadServices(false);
	UpdateFieldVisibility();
}

bool OBSStreamDestinationDialog::IsCustomService() const
{
	return serviceCombo->currentData().toInt() == (int)ListOpt::Custom;
}

bool OBSStreamDestinationDialog::IsWHIP() const
{
	return serviceCombo->currentData().toInt() == (int)ListOpt::WHIP;
}

bool OBSStreamDestinationDialog::IsAuthService() const
{
	if (IsCustomService() || IsWHIP()) {
		return false;
	}

	const std::string service = serviceCombo->currentText().toStdString();
	return Auth::AuthType(service) != Auth::Type::None;
}

void OBSStreamDestinationDialog::LoadServices(bool showAll)
{
	OBSProperties props = obs_get_service_properties("rtmp_common");
	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_bool(settings, "show_all", showAll);

	obs_property_t *prop = obs_properties_get(props, "show_all");
	obs_property_modified(prop, settings);

	serviceCombo->blockSignals(true);
	serviceCombo->clear();

	QStringList names;
	obs_property_t *services = obs_properties_get(props, "service");
	size_t services_count = obs_property_list_item_count(services);
	for (size_t i = 0; i < services_count; i++) {
		names.push_back(obs_property_list_item_string(services, i));
	}

	if (showAll) {
		names.sort(Qt::CaseInsensitive);
	}

	for (QString &name : names) {
		serviceCombo->addItem(name);
	}

	if (obs_is_output_protocol_registered("WHIP")) {
		serviceCombo->addItem(QTStr("WHIP"), QVariant((int)ListOpt::WHIP));
	}

	if (!showAll) {
		serviceCombo->addItem(QTStr("Basic.AutoConfig.StreamPage.Service.ShowAll"),
				      QVariant((int)ListOpt::ShowAll));
	}

	serviceCombo->insertItem(0, QTStr("Basic.AutoConfig.StreamPage.Service.Custom"), QVariant((int)ListOpt::Custom));

	if (!lastService.isEmpty()) {
		int idx = serviceCombo->findText(lastService);
		if (idx != -1) {
			serviceCombo->setCurrentIndex(idx);
		}
	}

	serviceCombo->blockSignals(false);
}

void OBSStreamDestinationDialog::UpdateServerList()
{
	QString serviceName = serviceCombo->currentText();
	if (IsCustomService() || IsWHIP()) {
		lastService = serviceName;
		return;
	}

	lastService = serviceName;

	OBSProperties props = obs_get_service_properties("rtmp_common");
	obs_property_t *services = obs_properties_get(props, "service");

	OBSDataAutoRelease settings = obs_data_create();
	obs_data_set_string(settings, "service", QT_TO_UTF8(serviceName));
	obs_property_modified(services, settings);

	obs_property_t *servers = obs_properties_get(props, "server");
	serverCombo->clear();

	size_t servers_count = obs_property_list_item_count(servers);
	for (size_t i = 0; i < servers_count; i++) {
		const char *name = obs_property_list_item_name(servers, i);
		const char *server = obs_property_list_item_string(servers, i);
		serverCombo->addItem(name, server);
	}
}

void OBSStreamDestinationDialog::OnAuthConnected()
{
	auto *oauth = dynamic_cast<OAuthStreamKey *>(auth.get());
	if (!oauth) {
		authStack->setCurrentIndex((int)AuthPage::Destination);
		return;
	}

	if (!oauth->key().empty()) {
		keyEdit->setText(QString::fromStdString(oauth->key()));
	}

	authStack->setCurrentIndex((int)AuthPage::Destination);
}

void OBSStreamDestinationDialog::UpdateFieldVisibility()
{
	const bool custom = IsCustomService();
	const bool whip = IsWHIP();
	const bool authService = IsAuthService() && cef;

	if (authService && keyEdit->text().isEmpty()) {
		authStack->setCurrentIndex((int)AuthPage::Connect);
	} else if (!authService) {
		authStack->setCurrentIndex((int)AuthPage::Destination);
	}

	const bool showDestination = authStack->currentIndex() == (int)AuthPage::Destination;
	serverStack->setVisible(showDestination);
	serverStack->setCurrentIndex(custom || whip ? 1 : 0);
	keyLabel->setVisible(showDestination);
	keyEdit->setVisible(showDestination);
	nameEdit->setVisible(showDestination);

	keyLabel->setText(whip ? QTStr("Basic.Settings.Stream.WHIPBearerToken")
			      : QTStr("Basic.AutoConfig.StreamPage.StreamKey"));

	useAuthCheck->setVisible(showDestination && custom);
	authUsernameEdit->setVisible(showDestination && custom);
	authPwEdit->setVisible(showDestination && custom);
	useAuthCheck->setEnabled(custom);
	authUsernameEdit->setEnabled(custom && useAuthCheck->isChecked());
	authPwEdit->setEnabled(custom && useAuthCheck->isChecked());

	connectAccountButton->setVisible(authService);
	useStreamKeyButton->setVisible(authService);
}

bool OBSStreamDestinationDialog::Validate() const
{
	if (IsWHIP()) {
		if (customServerEdit->text().trimmed().isEmpty()) {
			OBSMessageBox::warning(const_cast<OBSStreamDestinationDialog *>(this),
					       QTStr("Basic.Settings.Stream.AddDestination"),
					       QTStr("Basic.Settings.Stream.MissingUrl"));
			return false;
		}
		if (keyEdit->text().isEmpty()) {
			OBSMessageBox::warning(const_cast<OBSStreamDestinationDialog *>(this),
					       QTStr("Basic.Settings.Stream.AddDestination"),
					       QTStr("Basic.Settings.Stream.WHIPMissingBearerToken"));
			return false;
		}
		return true;
	}

	if (IsCustomService()) {
		if (customServerEdit->text().trimmed().isEmpty()) {
			OBSMessageBox::warning(const_cast<OBSStreamDestinationDialog *>(this),
					       QTStr("Basic.Settings.Stream.AddDestination"),
					       QTStr("Basic.Settings.Stream.MissingUrl"));
			return false;
		}
	} else if (serverCombo->count() == 0) {
		OBSMessageBox::warning(const_cast<OBSStreamDestinationDialog *>(this),
				       QTStr("Basic.Settings.Stream.AddDestination"),
				       QTStr("Basic.Settings.Stream.MissingUrl"));
		return false;
	}

	if (keyEdit->text().isEmpty()) {
		OBSMessageBox::warning(const_cast<OBSStreamDestinationDialog *>(this),
				       QTStr("Basic.Settings.Stream.AddDestination"),
				       QTStr("Basic.Settings.Stream.MissingStreamKey"));
		return false;
	}

	return true;
}

std::string OBSStreamDestinationDialog::DefaultDisplayName() const
{
	if (IsCustomService() || IsWHIP()) {
		return customServerEdit->text().trimmed().toStdString();
	}
	return serviceCombo->currentText().toStdString();
}

bool OBSStreamDestinationDialog::GetDestination(QWidget *parent, OBSStreamDestinationResult &result)
{
	OBSStreamDestinationDialog dialog(parent);
	if (dialog.exec() != QDialog::Accepted) {
		return false;
	}

	const bool custom = dialog.IsCustomService();
	const bool whip = dialog.IsWHIP();
	const char *service_id = whip ? "whip_custom" : (custom ? "rtmp_custom" : "rtmp_common");

	OBSDataAutoRelease settings = obs_data_create();
	if (whip) {
		obs_data_set_string(settings, "service", "WHIP");
		obs_data_set_string(settings, "server", QT_TO_UTF8(dialog.customServerEdit->text().trimmed()));
		obs_data_set_string(settings, "bearer_token", QT_TO_UTF8(dialog.keyEdit->text()));
	} else if (custom) {
		obs_data_set_string(settings, "server", QT_TO_UTF8(dialog.customServerEdit->text().trimmed()));
		obs_data_set_bool(settings, "use_auth", dialog.useAuthCheck->isChecked());
		if (dialog.useAuthCheck->isChecked()) {
			obs_data_set_string(settings, "username", QT_TO_UTF8(dialog.authUsernameEdit->text()));
			obs_data_set_string(settings, "password", QT_TO_UTF8(dialog.authPwEdit->text()));
		}
	} else {
		obs_data_set_string(settings, "service", QT_TO_UTF8(dialog.serviceCombo->currentText()));
		obs_data_set_string(settings, "server", QT_TO_UTF8(dialog.serverCombo->currentData().toString()));
		obs_data_set_string(settings, "key", QT_TO_UTF8(dialog.keyEdit->text()));
	}

	std::string displayName = dialog.nameEdit->text().trimmed().toStdString();
	if (displayName.empty()) {
		displayName = dialog.DefaultDisplayName();
	}

	result.displayName = displayName;
	result.type = service_id;
	result.settings = OBSData(settings.Get());
	return true;
}
