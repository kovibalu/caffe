#include <vector>

#include "hdf5.h"
#include "hdf5_hl.h"

#include "caffe/blob.hpp"
#include "caffe/common.hpp"
#include "caffe/layer.hpp"
#include "caffe/util/io.hpp"
#include "caffe/vision_layers.hpp"

namespace caffe {

template <typename Dtype>
ImageOutputLayer<Dtype>::ImageOutputLayer(const LayerParameter& param)
    : Layer<Dtype>(param),
      file_name_(param.image_output_param().file_name()),
      counter_(0) {
}

template <typename Dtype>
ImageOutputLayer<Dtype>::~ImageOutputLayer<Dtype>() {
}

template <typename Dtype>
cv::Mat ImageOutputLayer<Dtype>::ConvertBlobToCVImg(const Blob<Dtype>& blob, const int currentNum, const bool isCpu,
		const double upscale, const double mean_to_add) {
  const Dtype* blob_ptr;
  // Atm isCpu is not used, we copy data back from the GPU to the CPU
  blob_ptr = blob.cpu_data();

  const int channels = blob.channels();
  const int height = blob.height();
  const int width = blob.width();

  cv::Mat cv_img;
  if (channels == 1) {
  	  cv_img = cv::Mat(height, width, CV_8UC1);
  } else if (channels == 3) {
  	  cv_img = cv::Mat(height, width, CV_8UC3);
  } else {
  	  LOG(ERROR) << "The image has " << channels << " channels instead of 1 or 3, skipping";
  	  return cv_img;
  }

  int top_index;
  for (int h = 0; h < height; ++h) {
    uchar* ptr = cv_img.ptr<uchar>(h);
    int img_index = 0;
    for (int w = 0; w < width; ++w) {
      for (int c = 0; c < channels; ++c) {
	    top_index = blob.offset(currentNum, c, h, w);
        ptr[img_index++] = static_cast<uchar>(std::min(255.0, std::max(0.0, blob_ptr[top_index]*upscale + mean_to_add)));
      }
    }
  }

  return cv_img;
}
template <typename Dtype>
void ImageOutputLayer<Dtype>::Forward_helper(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top, bool isCpu) {
  CHECK_GE(bottom.size(), 1);
  int display = this->layer_param_.image_output_param().display();
  int trafo_count = this->layer_param_.image_output_param().transformation_size();

  if (this->counter_ % display == 0) {
	  for (int i = 0; i < bottom.size(); ++i) {
		  CHECK_GE(bottom[i]->channels(), 1);
		  CHECK_LE(bottom[i]->channels(), 3);

	  	  int trafo_index = std::min(trafo_count - 1, i);
		  const double upscale = this->layer_param_.image_output_param().transformation(trafo_index).upscale();
		  const double mean_to_add = this->layer_param_.image_output_param().transformation(trafo_index).mean_to_add();
		  for (int n = 0; n < bottom[i]->num(); ++n) {
			  cv::Mat cv_img = this->ConvertBlobToCVImg(*bottom[i], n, isCpu, upscale, mean_to_add);
			  stringstream ss;
			  ss << this->file_name_ << "-it" << this->counter_ << "-batchid" << n << "-bottom" << i << ".jpg";
			  WriteImageFromCVMat(ss.str(), cv_img);
			  LOG(INFO) << "Successfully saved one batch slice to " << ss.str();
		  }
	  }
  }
  this->counter_++;
}

template <typename Dtype>
void ImageOutputLayer<Dtype>::Forward_cpu(const vector<Blob<Dtype>*>& bottom,
      const vector<Blob<Dtype>*>& top) {
    this->Forward_helper(bottom, top, true);
}

template <typename Dtype>
void ImageOutputLayer<Dtype>::Backward_cpu(const vector<Blob<Dtype>*>& top,
      const vector<bool>& propagate_down, const vector<Blob<Dtype>*>& bottom) {
  return;
}

#ifdef CPU_ONLY
STUB_GPU(ImageOutputLayer);
#endif

INSTANTIATE_CLASS(ImageOutputLayer);
REGISTER_LAYER_CLASS(IMAGE_OUTPUT, ImageOutputLayer);
}  // namespace caffe