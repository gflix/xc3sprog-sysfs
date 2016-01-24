/* JTAG GNU/Linux parport device io

Copyright (C) 2004 Andrew Rogers
Additions for Byte Blaster Cable (C) 2005-2011  Uwe Bonnes 
                              bon@elektron.ikp.physik.tu-darmstadt.de

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA

Changes:
Dmitry Teytelman [dimtey@gmail.com] 14 Jun 2006 [applied 13 Aug 2006]:
    Code cleanup for clean -Wall compile.
    Changes to support new IOBase interface.
    Support for byte counting and progress bar.
*/

#include <sys/stat.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <iostream>
#include "iorpi.h"

#define SYSFS_GPIO_BASE "/sys/class/gpio"
#define SYSFS_GPIO_EXPORT SYSFS_GPIO_BASE "/export"
#define SYSFS_GPIO_UNEXPORT SYSFS_GPIO_BASE "/unexport"
#define SYSFS_GPIO SYSFS_GPIO_BASE "/gpio"
#define SYSFS_GPIO_DIRECTION "/direction"
#define SYSFS_GPIO_VALUE "/value"

#define DELAY_USECONDS 10

using namespace std;

IORPi::IORPi() : IOBase(), total(0), debug(0), gpioTck(NULL), gpioTms(NULL), gpioTdo(NULL), gpioTdi(NULL)
{
//	cout << "IORPi::IORPi()" << endl;
}

int IORPi::Init(struct cable_t *cable, const char *dev, unsigned int freq)
{
//	cout << "IORPi::Init()" << endl;
	gpioTck = new SysfsGpio("11");
	gpioTms = new SysfsGpio("25");
	gpioTdo = new SysfsGpio("9");
	gpioTdi = new SysfsGpio("10");

	if (
		(!gpioTck->exportGpio()) ||
		(!gpioTms->exportGpio()) ||
		(!gpioTdo->exportGpio()) ||
		(!gpioTdi->exportGpio())) {
		fprintf(stderr, "Could not export all needed GPIOs! Aborting.\n");
		return -1;
	}

	if (
		(!gpioTck->setDirection(GPIO_OUT)) ||
		(!gpioTms->setDirection(GPIO_OUT)) ||
		(!gpioTdo->setDirection(GPIO_IN)) ||
		(!gpioTdi->setDirection(GPIO_OUT))) {
		fprintf(stderr, "Could not set direction for all needed GPIOs! Aborting.\n");
		return -1;
	}

	gpioTck->setValue(false);
	gpioTms->setValue(true);
	gpioTdi->setValue(false);

	if (verbose) {
		fprintf(stderr,"Initialization of RPi GPIOs finished!\n");
	}

	return 0;
}

bool IORPi::txrx(bool tms, bool tdi)
{
	gpioTdi->setValue(tdi);
	gpioTms->setValue(tms);

	usleep(DELAY_USECONDS);
	gpioTck->setValue(true);
	total++;

	usleep(DELAY_USECONDS);
	bool returnValue = gpioTdo->getValue();
	gpioTck->setValue(false);

	if (verbose) {
		cout << "IORPi::txrx(" << string(tms ? "true" : "false") << ", " << string(tdi ? "true" : "false") << ")=" << string(returnValue ? "true" : "false") << endl;
	}

	return returnValue;
}

void IORPi::tx(bool tms, bool tdi)
{
	txrx(tms, tdi);
}

void IORPi::tx_tdi_byte(unsigned char tdi_byte)
{
	if (verbose) {
		cout << "IORPi::tx_tdi_byte(" << static_cast<int>(tdi_byte) << ")" << endl;
	}

	for (int k = 0; k < 8; k++) {
		tx(false, (tdi_byte >> k) & 1);
	}
}
 
void IORPi::txrx_block(const unsigned char *tdi, unsigned char *tdo,
			int length, bool last)
{
	if (verbose) {
		cout << "IORPi::txrx_block(***, ***, " << length << ", " << string(last ? "true" : "false") << ")" << endl;
	}

	int i = 0;
	int j = 0;
	unsigned char tdo_byte = 0;
	unsigned char tdi_byte;

	if (tdi) {
		tdi_byte = tdi[j];
	}

	while(i < length - 1) {
		tdo_byte = tdo_byte + (txrx(false, (tdi_byte & 1) == 1) << (i % 8));
		if (tdi) {
			tdi_byte = tdi_byte >> 1;
		}
		i++;
		if((i % 8) == 0) { // Next byte
			if(tdo) {
				tdo[j] = tdo_byte; // Save the TDO byte
			}
			tdo_byte = 0;
			j++;
			if (tdi) {
				tdi_byte = tdi[j]; // Get the next TDI byte
			}
		}
	};
	tdo_byte = tdo_byte + (txrx(last, (tdi_byte & 1) == 1) << (i % 8)); 
	if(tdo) {
		tdo[j] = tdo_byte;
	}

	gpioTck->setValue(false); /* Make sure, TCK is low */
	return;
}

void IORPi::tx_tms(unsigned char *pat, int length, int force)
{
	if (verbose) {
		cout << "IORPi::tx_tms(***, " << length << ", ***)" << endl;
	}

	int i;
	unsigned char tms;
	for (i = 0; i < length; i++) {
		if ((i & 0x7) == 0) {
			tms = pat[i>>3];
		}
		tx((tms & 0x01), true);
		tms = tms >> 1;
	}
	gpioTck->setValue(false); /* Make sure, TCK is low */
}

IORPi::~IORPi()
{
//	cout << "IORPi::~IORPi()" << endl;
	gpioTck->unexportGpio();
	gpioTms->unexportGpio();
	gpioTdo->unexportGpio();
	gpioTdi->unexportGpio();

	delete gpioTck;
	delete gpioTms;
	delete gpioTdo;
	delete gpioTdi;

	if (verbose) {
		fprintf(stderr, "Total bytes sent: %d\n", total>>3);
	}
}

bool SysfsGpio::setupGpio(const string &exportFile)
{
	errno = 0;
	FILE *exportFd = fopen(exportFile.c_str(), "w");
	int errorNumber = errno;
	if (exportFd) {
		fprintf(exportFd, "%s\n", gpioNumber.c_str());
		fclose(exportFd);
		errorNumber = errno;
	} else {
		cerr << "Error opening file [" << exportFile << "]! Aborting." << endl;
		return false;
	}
	if (errorNumber != 0) {
		cerr << "Error writing to file [" << exportFile << "]: " << strerror(errorNumber) << endl;
		return false;
	}
	return true;
}

bool SysfsGpio::exportGpio()
{
	string gpioDirectory = string(SYSFS_GPIO) + gpioNumber;
	struct stat gpioStat;
	if (stat(gpioDirectory.c_str(), &gpioStat) == 0) {
		cout << "GPIO " << gpioNumber << " was already exported." << endl;
		isExported = true;
		return true;
	}

	isExported = setupGpio(SYSFS_GPIO_EXPORT);
	return isExported;
}

bool SysfsGpio::unexportGpio()
{
	if (valueFd) {
		fclose(valueFd);
		valueFd = NULL;
	}
	if (!isExported) {
		return true;
	}
	isExported = false;
	return setupGpio(SYSFS_GPIO_UNEXPORT);
}

SysfsGpio::SysfsGpio(string gpioNumber):
	gpioNumber(gpioNumber), valueFd(NULL)
{
}

SysfsGpio::~SysfsGpio()
{
	unexportGpio();
}

bool SysfsGpio::setDirection(GpioDirection direction)
{
	if (!isExported) {
		return false;
	}
	string directionFile = string(SYSFS_GPIO) + gpioNumber + string(SYSFS_GPIO_DIRECTION);
	FILE *directionFd = fopen(directionFile.c_str(), "w");
	int errorNumber = errno;
	if (directionFd) {
		switch (direction) {
		case GPIO_IN:
			fprintf(directionFd, "in\n");
			break;
		case GPIO_OUT:
			fprintf(directionFd, "out\n");
			break;
		}
		fclose(directionFd);
		errorNumber = errno;
	} else {
		cerr << "Error opening file [" << directionFile << "]! Aborting." << endl;
		return false;
	}
	if (errorNumber != 0) {
		cerr << "Error writing to file [" << directionFile << "]: " << strerror(errorNumber) << endl;
		return false;
	}
	return true;
}

bool SysfsGpio::setValue(bool value)
{
	if (!isExported) {
		return false;
	}

	if (!valueFd) {
		string valueFile = string(SYSFS_GPIO) + gpioNumber + string(SYSFS_GPIO_VALUE);
		valueFd = fopen(valueFile.c_str(), "w+");
		if (!valueFd) {
			cerr << "Error opening file [" << valueFile << "]! Aborting." << endl;
			return false;
		}
	}

	rewind(valueFd);
	fprintf(valueFd, "%d\n", value ? 1 : 0);
	fflush(valueFd);

	return true;
}

bool SysfsGpio::getValue() {
	if (!isExported) {
		return false;
	}

	if (!valueFd) {
		string valueFile = string(SYSFS_GPIO) + gpioNumber + string(SYSFS_GPIO_VALUE);
		valueFd = fopen(valueFile.c_str(), "w+");
		if (!valueFd) {
			cerr << "Error opening file [" << valueFile << "]! Aborting." << endl;
			return false;
		}
	}

	rewind(valueFd);
	int rawValue = 0;
	fscanf(valueFd, "%d", &rawValue);
	fflush(valueFd);

	return (rawValue != 0);
}
