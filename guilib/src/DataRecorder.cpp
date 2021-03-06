/*
Copyright (c) 2010-2014, Mathieu Labbe - IntRoLab - Universite de Sherbrooke
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the Universite de Sherbrooke nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include "rtabmap/gui/DataRecorder.h"

#include <rtabmap/utilite/ULogger.h>
#include <rtabmap/utilite/UTimer.h>
#include <rtabmap/core/Memory.h>
#include <rtabmap/core/Signature.h>
#include <rtabmap/core/CameraEvent.h>
#include <rtabmap/core/util3d.h>
#include <rtabmap/gui/ImageView.h>
#include <rtabmap/gui/UCv2Qt.h>
#include <QtCore/QMetaType>
#include <QMessageBox>
#include <QtGui/QCloseEvent>
#include <QLabel>
#include <QVBoxLayout>

namespace rtabmap {


DataRecorder::DataRecorder(QWidget * parent) :
		QWidget(parent),
	memory_(0),
	imageView_(new ImageView(this)),
	label_(new QLabel(this)),
	processingImages_(false),
	count_(0),
	totalSizeKB_(0)
{
	qRegisterMetaType<cv::Mat>("cv::Mat");

	imageView_->setImageDepthShown(true);
	imageView_->setMinimumSize(320, 240);
	QVBoxLayout * layout = new QVBoxLayout(this);
	layout->setMargin(0);
	layout->addWidget(imageView_);
	layout->addWidget(label_);
	layout->setStretch(0,1);
	this->setLayout(layout);
}
bool DataRecorder::init(const QString & path, bool recordInRAM)
{
	UScopeMutex scope(memoryMutex_);
	if(!memory_)
	{
		ParametersMap customParameters;
		customParameters.insert(ParametersPair(Parameters::kMemRehearsalSimilarity(), "1.0")); // desactivate rehearsal
		customParameters.insert(ParametersPair(Parameters::kKpWordsPerImage(), "-1")); // desactivate keypoints extraction
		customParameters.insert(ParametersPair(Parameters::kMemBinDataKept(), "true")); // to keep images
		if(!recordInRAM)
		{
			customParameters.insert(ParametersPair(Parameters::kDbSqlite3InMemory(), "false"));
		}
		memory_ = new Memory();
		if(!memory_->init(path.toStdString(), true, customParameters))
		{
			delete memory_;
			memory_ = 0;
			UERROR("Error initializing the memory.");
			return false;
		}
		path_ = path;
		return true;
	}
	else
	{
		UERROR("Already initialized, close it first.");
		return false;
	}
}

void DataRecorder::closeRecorder()
{
	memoryMutex_.lock();
	if(memory_)
	{
		delete memory_;
		memory_ = 0;
		UINFO("Data recorded to \"%s\".", this->path().toStdString().c_str());
	}
	memoryMutex_.unlock();
	processingImages_ = false;
	count_ = 0;
	totalSizeKB_ = 0;
	if(this->isVisible())
	{
		QMessageBox::information(this, tr("Data recorder"), tr("Data recorded to \"%1\".").arg(this->path()));
	}
}

DataRecorder::~DataRecorder()
{
	this->unregisterFromEventsManager();
	this->closeRecorder();
}

void DataRecorder::addData(const rtabmap::SensorData & data)
{
	memoryMutex_.lock();
	if(memory_)
	{
		if(memory_->getStMem().size() == 0 && data.id() > 0)
		{
			ParametersMap customParameters;
			customParameters.insert(ParametersPair(Parameters::kMemGenerateIds(), "false")); // use id from data
			memory_->parseParameters(customParameters);
		}

		//save to database
		UTimer time;
		memory_->update(data);
		const Signature * s = memory_->getLastWorkingSignature();
		totalSizeKB_ += (int)s->getImageCompressed().total()/1000;
		totalSizeKB_ += (int)s->getDepthCompressed().total()/1000;
		memory_->cleanup();

		if(++count_ % 30)
		{
			memory_->emptyTrash();
		}
		UDEBUG("Time to process a message = %f s", time.ticks());
	}
	memoryMutex_.unlock();
}

void DataRecorder::showImage(const cv::Mat & image, const cv::Mat & depth)
{
	processingImages_ = true;
	imageView_->setImage(uCvMat2QImage(image));
	imageView_->setImageDepth(uCvMat2QImage(depth));
	label_->setText(tr("Images=%1 (~%2 MB)").arg(count_).arg(totalSizeKB_/1000));
	processingImages_ = false;
}

void DataRecorder::closeEvent(QCloseEvent* event)
{
	this->closeRecorder();
	event->accept();
}

void DataRecorder::handleEvent(UEvent * event)
{
	if(memory_)
	{
		if(event->getClassName().compare("CameraEvent") == 0)
		{
			CameraEvent * camEvent = (CameraEvent*)event;
			if(camEvent->getCode() == CameraEvent::kCodeImageDepth ||
			   camEvent->getCode() == CameraEvent::kCodeImage)
			{
				if(camEvent->data().isValid())
				{
					UINFO("Receiving rate = %f Hz", 1.0f/timer_.ticks());
					this->addData(camEvent->data());

					if(!processingImages_ && this->isVisible() && camEvent->data().isValid())
					{
						processingImages_ = true;
						QMetaObject::invokeMethod(this, "showImage",
								Q_ARG(cv::Mat, camEvent->data().image()),
								Q_ARG(cv::Mat, camEvent->data().depthOrRightImage()));
					}
				}
			}
		}
	}
}

} /* namespace rtabmap */
