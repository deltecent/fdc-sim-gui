/**********************************************************************************
*
*  Altair FDC+ Serial Disk Simulator
*      This program simulates an FDC+ Enhanced Floppy Disk Controller
*      serial modes 6 and 7.
*
*     Version     Date        Author         Notes
*      1.0     10/15/2020     P. Linstruth   Original
*
***********************************************************************************
*
*  Communication with the server is over a serial port at 403.2K Baud, 8N1.
*  All transactions are initiated by the FDC. The second choice for baud rate
*  is 460.8K. Finally, 230.4K is the most likely supported baud rate on the PC
*  if 403.2K and 460.8K aren't avaialable.
*
*  403.2K is the preferred rate as it allows full-speed operation and is the
*  most accurate of the three baud rate choices on the FDC. 460.8K also allows
*  full speed operation, but the baud rate is off by about 3.5%. This works, but
*  is borderline. 230.4K is available on most all serial ports, is within 2% of
*  the FDC baud rate, but runs at 80%-90% of real disk speed.
*
*  FDC TO SERVER COMMANDS
*    Commands from the FDC to the server are fixed length, ten byte messages. The 
*    first four bytes are a command in ASCII, the remaining six bytes are grouped
*    as three 16 bit words (little endian). The checksum is the 16 bit sum of the
*    first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command       Parameter 1         Parameter 2           Checksum
*
*    Commands:
*      STAT - Provide and request drive status. The FDC sends the selected drive
*             number and head load status in Parameter 1 and the current track 
*             number in Parameter 2. The Server responds with drive mount status
*             (see below). The LSB of Parameter 1 contains the currently selected
*             drive number or 0xff is no drive is selected. The MSB of parameter 1
;             is non-zero if the head is loaded, zero if not loaded.
*
*             The FDC issues the STAT command about ten times per second so that
*             head status and track number information is updated quickly. The 
*             server may also want to assume the drive is selected, the head is
*             loaded, and update the track number whenever a READ is received.
*
*      READ - Read specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             length is in Parameter 2 and must be the track length. Also see
*             "Transfer of Track Data" below.
*
*      WRIT - Write specified track. Parameter 1 contains the drive number in the
*             MSNibble. The lower 12 bits contain the track number. Transfer length
*             must be track length. Server responds with WRIT response when ready
*             for the FDC to send the track of data. See "Transfer of Track Data" below.
*
*
*  SERVER TO FDC 
*    Reponses from the server to the FDC are fixed length, ten byte messages. The 
*    first four bytes are a response command in ASCII, the remaining six bytes are
*    grouped as three 16 bit words (little endian). The checksum is the 16 bit sum
*    of the first eight bytes of the message.
*
*    Bytes 0-3   Bytes 4-5 as Word   Bytes 6-7 as Word   Bytes 8-9 as Word
*    ---------   -----------------   -----------------   -----------------
*     Command      Response Code        Reponse Data          Checksum
*
*    Commands:
*      STAT - Returns drive status in Response Data with one bit per drive. "1" means a
*             drive image is mounted, "0" means not mounted. Bits 15-0 correspond to
*             drive numbers 15-0. Response code is ignored by the FDC.
*
*      WRIT - Issued in repsonse to a WRIT command from the FDC. This response is
*             used to tell the FDC that the server is ready to accept continuous transfer
*             of a full track of data (response code word set to "OK." If the request
*             can't be fulfilled (e.g., specified drive not mounted), the reponse code
*             is set to NOT READY. The Response Data word is don't care.
*
*      WSTA - Final status of the write command after receiving the track data is returned
*             in the repsonse code field. The Response Data word is don't care.
*
*    Reponse Code:
*      0x0000 - OK
*      0x0001 - Not Ready (e.g., write request to unmounted drive)
*      0x0002 - Checksum error (e.g., on the block of write data)
*      0x0003 - Write error (e.g., write to disk failed)
*
*
*  TRANSFER OF TRACK DATA
*    Track data is sent as a sequence of bytes followed by a 16 bit, little endian 
*    checksum. Note the Transfer Length field does NOT include the two bytes of
*    the checksum. The following notes apply to both the FDC and the server.
*
*  ERROR RECOVERY
*    The FDC uses a timeout of one second after the last byte of a message or data block
*        is sent to determine if a transmission was ignored.
*
*    The server should ignore commands with an invalid checksum. The FDC may retry the
*        command if no response is received. An invalid checksum on a block of write
*        data should not be ignored, instead, the WRIT response should have the
*        Reponse Code field set to 0x002, checksum error.
*
*    The FDC ignores responses with an invalid checksum. The FDC may retry the command
*        that generated the response by sending the command again.
*
***********************************************************************************/

#include <QtWidgets>
#include <QMessageBox>

#include "fdc-sim-gui.h"
#include "grnled.xpm"
#include "redled.xpm"

FDCDialog::FDCDialog(QWidget *parent)
	: QDialog(parent)
{
	// Title
	setWindowTitle(tr("FDC+ Serial Drive Simulator"));

	// Pixmaps
	grnLED = new QPixmap(greenled_xpm);
	redLED = new QPixmap(redled_xpm);

	// Layouts
	QVBoxLayout *mainLayout = new QVBoxLayout;
	QHBoxLayout *commLayout = new QHBoxLayout;
	QHBoxLayout *driveLayout = new QHBoxLayout;
	QHBoxLayout *trackLayout = new QHBoxLayout;
	QHBoxLayout *statLayout = new QHBoxLayout;
	QHBoxLayout *paramLayout = new QHBoxLayout;
	QHBoxLayout *buttonLayout = new QHBoxLayout;
	QHBoxLayout *infoLayout = new QHBoxLayout;

	// Information
	label = new QLabel(tr("FDC+ Serial Drive Simulator v1.0"));
	infoLayout->addWidget(label);
	label = new QLabel(tr("(c)2020 Deltec Enterprises"));
	label->setAlignment(Qt::AlignRight);
	infoLayout->addWidget(label);

	// Communications Ports
	serialPortBox = new QComboBox;
	serialPorts = QSerialPortInfo::availablePorts();
	for (const QSerialPortInfo &info : serialPorts) {
		serialPortBox->addItem(info.portName());
	}
	serialPortBox->setPlaceholderText(tr("None"));
	serialPortBox->setCurrentIndex(-1);
	connect(serialPortBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){ serialPortSlot(index); });

	commLayout->addWidget(serialPortBox);

	baudRateBox = new QComboBox;
	baudRateBox->addItem("230.4K", 230400);
	baudRateBox->addItem("403.2K", 403200);
	baudRateBox->addItem("460.8K", 460800);
	connect(baudRateBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){ baudRateSlot(index); });

	commLayout->addWidget(baudRateBox);

	// Disk Type
	diskBox = new QComboBox;
	diskBox->addItem("8 Inch", TRACK_LEN_8);
	diskBox->addItem("Minidisk", TRACK_LEN_5);
	connect(diskBox, QOverload<int>::of(&QComboBox::currentIndexChanged), [this](int index){ diskSlot(index); });

	commLayout->addWidget(diskBox);

	mainLayout->addLayout(commLayout);

	// Drive, Track, and STAT timer parameters
	label = new QLabel(tr("Drive Number:"));
	driveLayout->addWidget(label);
	driveNumEdit = new QLineEdit();
	driveLayout->addWidget(driveNumEdit);
	connect(driveNumEdit, &QLineEdit::textChanged, this, &FDCDialog::driveNumEditSlot);

	label = new QLabel(tr("Track Number:"));
	driveLayout->addWidget(label);
	trackNumEdit = new QLineEdit();
	trackNumEdit->setText("0");
	trackLayout->addWidget(trackNumEdit);
	connect(trackNumEdit, &QLineEdit::textChanged, this, &FDCDialog::trackNumEditSlot);

	label = new QLabel(tr("STAT Timer (ms):"));
	statLayout->addWidget(label);
	statTimerEdit = new QLineEdit();
	statTimerEdit->setText("100");
	statLayout->addWidget(statTimerEdit);
	label = new QLabel(tr("Auto"));
	statLayout->addWidget(label);
	statAutoCheck = new QCheckBox;
	statLayout->addWidget(statAutoCheck);
	connect(statAutoCheck, QOverload<int>::of(&QCheckBox::stateChanged), [this](int state){ statAutoCheckSlot(state); });
	connect(statTimerEdit, &QLineEdit::textChanged, this, &FDCDialog::statTimerEditSlot);


	paramLayout->addLayout(driveLayout);
	paramLayout->addLayout(trackLayout);
	paramLayout->addLayout(statLayout);
	mainLayout->addLayout(paramLayout);

	// Command Buttons
	statButton = new QPushButton(tr("STAT"));
	readButton = new QPushButton(tr("READ"));
	writButton = new QPushButton(tr("WRIT"));

	buttonLayout->addWidget(statButton);
	buttonLayout->addWidget(readButton);
	buttonLayout->addWidget(writButton);
	
	mainLayout->addLayout(buttonLayout);

	connect(statButton, &QPushButton::clicked, this, &FDCDialog::statButtonSlot);
	connect(readButton, &QPushButton::clicked, this, &FDCDialog::readButtonSlot);
	connect(writButton, &QPushButton::clicked, this, &FDCDialog::writButtonSlot);

	// Message Line
	messageLabel = new QLabel;
	mainLayout->addWidget(messageLabel);

	// Information Line
	mainLayout->addLayout(infoLayout);

	setLayout(mainLayout);

	// Serial Port Object
	serialPort = new QSerialPort;
	baudRate = baudRateBox->currentData().toInt();

	// Initialize heads
	for (driveNum = 0; driveNum < MAX_DRIVE; driveNum++) {
		headStatus[driveNum] = 0;
	}

	driveNum = 0xff;
	trackNum = 0;
	trackMax = TRACK_MAX_8;
	trackLen = TRACK_LEN_8;

	// Start timer
	timer = new QTimer(this);
	timer->setInterval(statTimerEdit->text().toInt());
	connect(timer, &QTimer::timeout, this, &FDCDialog::timerSlot);
	timer->start();
}

void FDCDialog::diskSlot(int index)
{
	if ((trackLen = diskBox->itemData(index).toInt()) == TRACK_LEN_8) {
		trackMax = TRACK_MAX_8;
	}
	else {
		trackMax = TRACK_MAX_5;
	}
}

void FDCDialog::serialPortSlot(int index)
{
	serialPort->setPortName(serialPortBox->itemText(index));

	updateSerialPort();
}

void FDCDialog::baudRateSlot(int index)
{
	baudRate = baudRateBox->itemData(index).toInt();

	updateSerialPort();
}

void FDCDialog::driveNumEditSlot()
{
	int d;

	// Update drive number
	if (driveNumEdit->text().length() == 0) {
		driveNum = 0xff;
	}
	else {
		d = driveNumEdit->text().toInt();

//		if (d >= 0 && d < MAX_DRIVE) {
			driveNum = d;
//		}
	}
}

void FDCDialog::trackNumEditSlot()
{
	int t;

	t = trackNumEdit->text().toInt();

//	if (t >= 0 && t < trackMax) {
		trackNum = t;
//	}
}

void FDCDialog::statTimerEditSlot()
{
	int t;

	// Update timer interval
	if ((t = statTimerEdit->text().toInt()) >= 100) {
		timer->setInterval(t);
	}
}

void FDCDialog::statAutoCheckSlot(int state)
{
	statButton->setEnabled(!state);
}

void FDCDialog::statButtonSlot()
{
	statCmd();
}

void FDCDialog::readButtonSlot()
{
	readCmd();
}

void FDCDialog::writButtonSlot()
{
	writCmd();
}

void FDCDialog::timerSlot()
{
	if (!serialPort->isOpen()) {
		return;
	}

	if (statAutoCheck->isChecked()) {
		statCmd();
	}
}

void FDCDialog::updateSerialPort()
{
	if (serialPort->isOpen()) {
		serialPort->clear();
		serialPort->close();
	}

	if (serialPortBox->currentIndex() == -1) {
		return;
	}

	if (serialPort->open(QIODevice::ReadWrite)) {
		if (serialPort->setBaudRate(baudRate) == false) {
			QMessageBox::critical(this,
				"Serial Port Error",
				QString("Could not set baudrate to %1").arg(baudRate));
		}
		serialPort->setDataBits(QSerialPort::Data8);
		serialPort->setParity(QSerialPort::NoParity);
		serialPort->setStopBits(QSerialPort::OneStop);
		serialPort->setFlowControl(QSerialPort::NoFlowControl);
		serialPort->setDataTerminalReady(true);
		serialPort->setRequestToSend(true);
		serialPort->clear();
	}
	else {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString("Could not open serial port '%1' (%2)").arg(serialPort->portName()).arg(serialPort->error()));
		serialPortBox->setCurrentIndex(-1);
	}
}

void FDCDialog::statCmd()
{
        qint64 bytesAvail;
	int d;

	if (!serialPort->isOpen()) {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString(tr("Serial port not open")));

		return;
	}

	cmdBuf.command[0] = 'S';
	cmdBuf.command[1] = 'T';
	cmdBuf.command[2] = 'A';
	cmdBuf.command[3] = 'T';

	cmdBuf.param1 = driveNum;	// MSB head load, LSB drive number

	for (d = 0; d < MAX_DRIVE; d++) {
		cmdBuf.param1 |= (headStatus[d] != 0)  << d;
	}

	cmdBuf.param2 = 0;		// Track Number

	cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, COMMAND_LENGTH);

	serialPort->write((char *) cmdBuf.asBytes, CMDBUF_SIZE);

	// Wait for STAT response
	cmdBufIdx = 0;
	do {
		bytesAvail = serialPort->waitForReadyRead(500);
		cmdBufIdx += serialPort->read((char *) &cmdBuf.asBytes[cmdBufIdx], CMDBUF_SIZE-cmdBufIdx);
	} while (cmdBufIdx < CMDBUF_SIZE && cmdBufIdx != -1);

	if (cmdBufIdx == -1) {
		messageLabel->setText(QString("read() error"));
		return;
	}

	if (QString(cmdBuf.command).left(4) != QString("STAT")) {
		messageLabel->setText(QString("Did not receive 'STAT' response '%1'").arg(QString(cmdBuf.command).left(4)));
	}
	else if (statAutoCheck->isChecked() == false) {
		messageLabel->setText(QString("Received 'STAT' response 0x%1").arg(cmdBuf.rdata, 4, 16, QChar('0')));
	}
}

void FDCDialog::readCmd()
{
        qint64 bytesAvail;
	quint16 checksum;
	quint16 *p;

	if (!serialPort->isOpen()) {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString(tr("Serial port not open")));

		return;
	}

	if (driveNum < 0 || driveNum >= MAX_DRIVE) {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString(tr("Invalid drive number")));

		return;
	}

	cmdBuf.command[0] = 'R';
	cmdBuf.command[1] = 'E';
	cmdBuf.command[2] = 'A';
	cmdBuf.command[3] = 'D';
	cmdBuf.param1 = trackNum | (driveNum << 12);
	cmdBuf.param2 = trackLen;

	cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, COMMAND_LENGTH);

	serialPort->write((char *) cmdBuf.asBytes, CMDBUF_SIZE);

	trkBufIdx = 0;

	do {
		bytesAvail = serialPort->waitForReadyRead(100);
		trkBufIdx += serialPort->read((char *) &trackBuf[trkBufIdx], TRACKBUF_LEN_CRC-trkBufIdx);
	} while (trkBufIdx < trackLen + 2 && bytesAvail);

	if (trkBufIdx == trackLen + 2) {
		checksum = calcChecksum(trackBuf, TRACKBUF_LEN);
		p = (quint16 *) &trackBuf[trackLen];

		messageLabel->setText(QString("Received %1 byte track").arg(trackLen));
	}
	else if (trkBufIdx == -1) {
		messageLabel->setText(QString("read() error"));
	}
	else {
		messageLabel->setText(QString("Received %1 of %2 bytes").arg(trkBufIdx).arg(trackLen+2));
	}
}

void FDCDialog::writCmd()
{
        qint64 bytesAvail;
	quint16 checksum;

	if (!serialPort->isOpen()) {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString(tr("Serial port not open")));

		return;
	}

	if (driveNum < 0 || driveNum >= MAX_DRIVE) {
		QMessageBox::critical(this,
			"Serial Port Error",
			QString(tr("Invalid drive number")));

		return;
	}

	cmdBuf.command[0] = 'W';
	cmdBuf.command[1] = 'R';
	cmdBuf.command[2] = 'I';
	cmdBuf.command[3] = 'T';
	cmdBuf.param1 = trackNum | (driveNum << 12);
	cmdBuf.param2 = trackLen;

	cmdBuf.checksum = calcChecksum(cmdBuf.asBytes, COMMAND_LENGTH);

	serialPort->write((char *) cmdBuf.asBytes, CMDBUF_SIZE);

	// Wait for WRIT response
	cmdBufIdx = 0;
	do {
		bytesAvail = serialPort->waitForReadyRead(500);
		cmdBufIdx += serialPort->read((char *) &cmdBuf.asBytes[cmdBufIdx], CMDBUF_SIZE-cmdBufIdx);
	} while (cmdBufIdx < CMDBUF_SIZE && cmdBufIdx != -1);

	if (cmdBufIdx == -1) {
		messageLabel->setText(QString("read() error"));
		return;
	}

	if (QString(cmdBuf.command).left(4) != QString("WRIT")) {
		messageLabel->setText(QString("Did not receive 'WRIT' response '%1'").arg(QString(cmdBuf.command).left(4)));
		return;
	}

	if (cmdBuf.rcode == STAT_OK) {
		checksum = calcChecksum(trackBuf, trackLen);
		trackBuf[trackLen] = checksum & 0x00ff;                 // LSB of checksum
		trackBuf[trackLen+1] = (checksum >> 8) & 0x00ff;        // MSB of checksum

		serialPort->write((char *) trackBuf, trackLen + 2);
	}
	else {
		messageLabel->setText(QString("Received "));
		switch (cmdBuf.rcode) {
			case STAT_NOT_READY:
				messageLabel->setText(messageLabel->text() + QString("NOT READY"));
				break;
			case STAT_CHECKSUM_ERR:
				messageLabel->setText(messageLabel->text() + QString("CHECKSUM ERROR"));
				break;
			case STAT_WRITE_ERR:
				messageLabel->setText(messageLabel->text() + QString("WRITE ERROR"));
				break;
			default:
				messageLabel->setText(messageLabel->text() + QString("UNKNOWN"));
				break;
		}
		messageLabel->setText(messageLabel->text() + QString(" WSTA response"));

		return;
	}

	// Wait for WSTA response
	cmdBufIdx = 0;
	do {
		bytesAvail = serialPort->waitForReadyRead(500);
		cmdBufIdx += serialPort->read((char *) &cmdBuf.asBytes[cmdBufIdx], CMDBUF_SIZE-cmdBufIdx);
	} while (cmdBufIdx < CMDBUF_SIZE && cmdBufIdx != -1);

	if (cmdBufIdx == -1) {
		messageLabel->setText(QString("read() error"));
		return;
	}

	if (QString(cmdBuf.command).left(4) != QString("WSTA")) {
		messageLabel->setText(QString("Did not receive 'WSTA' response '%1'").arg(QString(cmdBuf.command).left(4)));
	}
	else {
		messageLabel->setText(QString("Received WSTA "));
		switch (cmdBuf.rcode) {
			case STAT_OK:
				messageLabel->setText(messageLabel->text() + QString("OK"));
				break;
			case STAT_NOT_READY:
				messageLabel->setText(messageLabel->text() + QString("NOT READY"));
				break;
			case STAT_CHECKSUM_ERR:
				messageLabel->setText(messageLabel->text() + QString("CHECKSUM ERROR"));
				break;
			case STAT_WRITE_ERR:
				messageLabel->setText(messageLabel->text() + QString("WRITE ERROR"));
				break;
			default:
				messageLabel->setText(messageLabel->text() + QString("UNKNOWN"));
				break;
		}
		messageLabel->setText(messageLabel->text() + QString(" response"));

		return;
	}
}

quint16 FDCDialog::calcChecksum(const quint8 *data, int length)
{
	int i;
	quint16 checksum;

	checksum = 0;

	for (i = 0; i < length; i++) {
		checksum += data[i];
	}

	return checksum;
}

int main(int argc, char **argv)
{
	QApplication app(argc, argv);
	app.setStyle(QStyleFactory::create("Fusion"));
	FDCDialog *dialog = new FDCDialog;
	dialog->show();
	return app.exec();
}


