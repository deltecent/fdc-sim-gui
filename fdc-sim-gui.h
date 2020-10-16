#ifndef FDCSDSGUI_H
#define FDCSDSGUI_H

#include <QDialog>
#include <QTimer>
#include <QFile>
#include <QLabel>
#include <QLCDNumber>
#include <QProgressBar>
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QComboBox>
#include <QCheckBox>
#include <QPixmap>
#include <QSerialPort>
#include <QSerialPortInfo>
#include <QList>

#define MAX_DRIVE		4
#define CMDBUF_SIZE		10
#define COMMAND_LENGTH		8                       // does not include checksum bytes
#define	TRACK_MAX_5		35			// Minidisk tracks
#define	TRACK_MAX_8		77			// 8" tracks
#define	TRACK_LEN_5		137*16			// Minidisk track length
#define	TRACK_LEN_8		137*32			// 8" track length
#define TRACKBUF_LEN		TRACK_LEN_8		// maximum valid track length
#define TRACKBUF_LEN_CRC	(TRACKBUF_LEN+2)	// maximum valid track length with CRC
#define STAT_OK			0x0000			// OK
#define STAT_NOT_READY		0x0001			// Not Ready
#define STAT_CHECKSUM_ERR	0x0002			// Checksum Error
#define STAT_WRITE_ERR		0x0003			// Write Error

typedef struct TCOMMAND {
	union {
		quint8 asBytes[CMDBUF_SIZE];
		struct {
			char command[4];
			union {
				quint16 param1;
				quint16 rcode;
			};
			union {
				quint16 param2;
				quint16 rdata;
			};
			quint16 checksum;
		};
	};
} tcommand_t;

class FDCDialog : public QDialog
{
	Q_OBJECT

public:
	FDCDialog(QWidget *parent = 0);

private slots:
	void diskSlot(int index);
	void serialPortSlot(int index);
	void baudRateSlot(int index);
	void timerSlot();
	void driveNumEditSlot();
	void trackNumEditSlot();
	void statTimerEditSlot();
	void statAutoCheckSlot(int state);
	void statButtonSlot();
	void readButtonSlot();
	void writButtonSlot();

private:
	quint8 driveNum;
	quint16 trackNum;
	tcommand_t cmdBuf;
	quint8 headStatus[MAX_DRIVE];
	quint8 trackBuf[TRACKBUF_LEN_CRC];
	qint16 trkBufIdx;
	qint16 cmdBufIdx;
	quint8 trackMax;
	quint16 trackLen;
	QTimer *timer;
	QComboBox *diskBox;
	QComboBox *serialPortBox;
	QComboBox *baudRateBox;
	QPushButton *statButton;
	QPushButton *readButton;
	QPushButton *writButton;
	QLabel *label;
	QList<QSerialPortInfo> serialPorts;
	QSerialPort *serialPort;
	quint32 baudRate;
	QIODevice::OpenMode openMode[MAX_DRIVE];
	const QPixmap *grnLED;
	const QPixmap *redLED;
	QLineEdit *driveNumEdit;
	QLineEdit *trackNumEdit;
	QLineEdit *statTimerEdit;
	QCheckBox *statAutoCheck;
	QLabel *messageLabel;
	quint32 hlTimeout;

	void statCmd(void);
	void readCmd(void);
	void writCmd(void);
	void updateSerialPort(void);
	quint16 calcChecksum(const quint8 *data, int length);
};

#endif

