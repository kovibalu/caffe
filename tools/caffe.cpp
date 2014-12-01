#include <glog/logging.h>

#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "caffe/caffe.hpp"
#include "caffe/util/io.hpp"
#include "caffe/util/math_functions.hpp"
#include "caffe/util/upgrade_proto.hpp"

using caffe::Blob;
using caffe::Caffe;
using caffe::Net;
using caffe::NetParameter;
using caffe::Layer;
using caffe::shared_ptr;
using caffe::Timer;
using caffe::vector;

DEFINE_int32(gpu, -1,
             "Run in GPU mode on given device ID.");
DEFINE_string(solver, "",
              "The solver definition protocol buffer text file.");
DEFINE_string(model, "",
              "The model definition protocol buffer text file..");
DEFINE_string(snapshot, "",
              "Optional; the snapshot solver state to resume training.");
DEFINE_string(weights, "",
              "Optional; the pretrained weights to initialize finetuning. "
              "Cannot be set simultaneously with snapshot.");
DEFINE_int32(iterations, 50,
             "The number of iterations to run.");

// Only for gradient and classimage
DEFINE_string(datalayer, "",
              "Optional; the name of the data layer where the gradient will be propagated back.");
DEFINE_string(visualizedlayer, "",
              "Optional; the name of the visualized layer.");
DEFINE_int32(datalayer_mean_to_add, 128,
             "Optional; this will be added to the data layer blob values after upscaling before saving as an image.");
DEFINE_int32(datalayer_upscale, 255,
             "Optional; the data layer blob values will be upscaled by this value before adding the mean and saving as an image.");
DEFINE_int32(gradient_upscale, 2,
             "Optional; the backpropagated gradient blob values will be upscaled by this value before adding the mean and saving as an image.");
DEFINE_int32(saliency_upscale, 3,
             "Optional; the backpropagated gradient blob values will be upscaled by this value before adding the mean and saving as an image.");
DEFINE_string(visdir, "",
              "Optional; path to directory where the visualizations will be saved, it should exist!");
DEFINE_bool(onlycmax, true,
            "Optional; if true we only compute the gradients for the maximum value through the channels in the measured blob.");
DEFINE_double(learningrate, 5,
            "Optional; the learning rate used in class image computation.");
DEFINE_double(weightdecay, 0.0005,
            "Optional; the weight decay used in class image computation.");

// A simple registry for caffe commands.
typedef int (*BrewFunction)();
typedef std::map<caffe::string, BrewFunction> BrewMap;
BrewMap g_brew_map;

#define RegisterBrewFunction(func) \
namespace { \
class __Registerer_##func { \
 public: /* NOLINT */ \
  __Registerer_##func() { \
    g_brew_map[#func] = &func; \
  } \
}; \
__Registerer_##func g_registerer_##func; \
}

static BrewFunction GetBrewFunction(const caffe::string& name) {
	if (g_brew_map.count(name)) {
		return g_brew_map[name];
	} else {
		LOG(ERROR) << "Available caffe actions:";
		for (BrewMap::iterator it = g_brew_map.begin();
		        it != g_brew_map.end(); ++it) {
			LOG(ERROR) << "\t" << it->first;
		}
		LOG(FATAL) << "Unknown action: " << name;
		return NULL;  // not reachable, just to suppress old compiler warnings.
	}
}

// caffe commands to call by
//     caffe <command> <args>
//
// To add a command, define a function "int command()" and register it with
// RegisterBrewFunction(action);

// Device Query: show diagnostic information for a GPU device.
int device_query() {
	CHECK_GT(FLAGS_gpu, -1) << "Need a device ID to query.";
	LOG(INFO) << "Querying device ID = " << FLAGS_gpu;
	caffe::Caffe::SetDevice(FLAGS_gpu);
	caffe::Caffe::DeviceQuery();
	return 0;
}
RegisterBrewFunction(device_query);


// Train / Finetune a model.
int train() {
	CHECK_GT(FLAGS_solver.size(), 0) << "Need a solver definition to train.";
	CHECK(!FLAGS_snapshot.size() || !FLAGS_weights.size())
	        << "Give a snapshot to resume training or weights to finetune "
	        "but not both.";

	caffe::SolverParameter solver_param;
	caffe::ReadProtoFromTextFileOrDie(FLAGS_solver, &solver_param);

	// If the gpu flag is not provided, allow the mode and device to be set
	// in the solver prototxt.
	if (FLAGS_gpu < 0
	        && solver_param.solver_mode() == caffe::SolverParameter_SolverMode_GPU) {
		FLAGS_gpu = solver_param.device_id();
	}

	// Set device id and mode
	if (FLAGS_gpu >= 0) {
		LOG(INFO) << "Use GPU with device ID " << FLAGS_gpu;
		Caffe::SetDevice(FLAGS_gpu);
		Caffe::set_mode(Caffe::GPU);
	} else {
		LOG(INFO) << "Use CPU.";
		Caffe::set_mode(Caffe::CPU);
	}

	LOG(INFO) << "Starting Optimization";
	shared_ptr<caffe::Solver<float> >
	solver(caffe::GetSolver<float>(solver_param));

	if (FLAGS_snapshot.size()) {
		LOG(INFO) << "Resuming from " << FLAGS_snapshot;
		solver->Solve(FLAGS_snapshot);
	} else if (FLAGS_weights.size()) {
		LOG(INFO) << "Finetuning from " << FLAGS_weights;
		solver->net()->CopyTrainedLayersFrom(FLAGS_weights);
		solver->Solve();
	} else {
		solver->Solve();
	}
	LOG(INFO) << "Optimization Done.";
	return 0;
}
RegisterBrewFunction(train);

// Test: score a model.
int test() {
	CHECK_GT(FLAGS_model.size(), 0) << "Need a model definition to score.";
	CHECK_GT(FLAGS_weights.size(), 0) << "Need model weights to score.";

	// Set device id and mode
	if (FLAGS_gpu >= 0) {
		LOG(INFO) << "Use GPU with device ID " << FLAGS_gpu;
		Caffe::SetDevice(FLAGS_gpu);
		Caffe::set_mode(Caffe::GPU);
	} else {
		LOG(INFO) << "Use CPU.";
		Caffe::set_mode(Caffe::CPU);
	}
	// Instantiate the caffe net.
	Caffe::set_phase(Caffe::TEST);
	Net<float> caffe_net(FLAGS_model);
	caffe_net.CopyTrainedLayersFrom(FLAGS_weights);
	LOG(INFO) << "Running for " << FLAGS_iterations << " iterations.";

	vector<Blob<float>* > bottom_vec;
	vector<int> test_score_output_id;
	vector<float> test_score;
	float loss = 0;
	for (int i = 0; i < FLAGS_iterations; ++i) {
		float iter_loss;
		const vector<Blob<float>*>& result =
		    caffe_net.Forward(bottom_vec, &iter_loss);
		loss += iter_loss;
		int idx = 0;
		for (int j = 0; j < result.size(); ++j) {
			const float* result_vec = result[j]->cpu_data();
			for (int k = 0; k < result[j]->count(); ++k, ++idx) {
				const float score = result_vec[k];
				if (i == 0) {
					test_score.push_back(score);
					test_score_output_id.push_back(j);
				} else {
					test_score[idx] += score;
				}
				const std::string& output_name = caffe_net.blob_names()[
				                                     caffe_net.output_blob_indices()[j]];
				LOG(INFO) << "Batch " << i << ", " << output_name << " = " << score;
			}
		}
	}
	loss /= FLAGS_iterations;
	LOG(INFO) << "Loss: " << loss;
	for (int i = 0; i < test_score.size(); ++i) {
		const std::string& output_name = caffe_net.blob_names()[
		                                     caffe_net.output_blob_indices()[test_score_output_id[i]]];
		const float loss_weight =
		    caffe_net.blob_loss_weights()[caffe_net.output_blob_indices()[i]];
		std::ostringstream loss_msg_stream;
		const float mean_score = test_score[i] / FLAGS_iterations;
		if (loss_weight) {
			loss_msg_stream << " (* " << loss_weight
			                << " = " << loss_weight * mean_score << " loss)";
		}
		LOG(INFO) << output_name << " = " << mean_score << loss_msg_stream.str();
	}

	return 0;
}
RegisterBrewFunction(test);

struct VisData {
	Net<float>* caffe_net;

	Blob<float>* dataBlob;
	int dataLayerid;

	Blob<float>* visBlob;
	int visLayerid;

	VisData() {
		caffe_net = NULL;
		dataBlob = NULL;
		visBlob = NULL;
	}

	~VisData() {
		delete caffe_net;
	}
};

VisData* locateDataAndVisualizedLayers() {
	VisData* ret = new VisData();

	NetParameter param;
	caffe::ReadNetParamsFromTextFileOrDie(FLAGS_model, &param);
	// Set force backward since we have to compute backward for the data layer
	param.set_force_backward(true);
	ret->caffe_net = new Net<float>(param);

	// We switch on debug_info to see every detail during forward and back propagation
	ret->caffe_net->set_debug_info(true);

	ret->caffe_net->CopyTrainedLayersFrom(FLAGS_weights);

	ret->dataLayerid = ret->caffe_net->layerid_by_name(FLAGS_datalayer);
	// If we couldn't find a layer with this name, maybe we can find a blob!
	// If this is a deploy network definition there will probably be a blob instead of a layer
	if (ret->dataLayerid == -1) {
		int dataBlobid = ret->caffe_net->blobid_by_name(FLAGS_datalayer);
		CHECK(dataBlobid != -1) << "Invalid data name, couldn't find a layer or blob with this name!";

		ret->dataBlob = ret->caffe_net->blobs()[dataBlobid].get();
	} else {
		// We assume that the first top of the data layer contains the input image
		ret->dataBlob = ret->caffe_net->top_vecs()[ret->dataLayerid][0];
	}

	LOG(INFO) << "Data blob dimensions:";
	LOG(INFO) << "num: " << ret->dataBlob->num();
	LOG(INFO) << "channels: " << ret->dataBlob->channels();
	LOG(INFO) << "height: " << ret->dataBlob->height();
	LOG(INFO) << "width: " << ret->dataBlob->width();

	ret->visLayerid = ret->caffe_net->layerid_by_name(FLAGS_visualizedlayer);
	CHECK(ret->visLayerid != -1) << "Invalid measured name, couldn't find a layer with this name!";
	// We assume that the first top of the measured layer contains the measured blob
	ret->visBlob = ret->caffe_net->top_vecs()[ret->visLayerid][0];

	LOG(INFO) << "Measured blob dimensions:";
	LOG(INFO) << "num: " << ret->visBlob->num();
	LOG(INFO) << "channels: " << ret->visBlob->channels();
	LOG(INFO) << "height: " << ret->visBlob->height();
	LOG(INFO) << "width: " << ret->visBlob->width();

	return ret;
}

// gradient: visualize the gradients of a model.
int gradient() {
	CHECK_GT(FLAGS_model.size(), 0) << "Need a model definition for gradient.";
	CHECK_GT(FLAGS_weights.size(), 0) << "Need model weights for gradient.";
	CHECK_GT(FLAGS_datalayer.size(), 0) << "Need data layer name for gradient.";
	CHECK_GT(FLAGS_visualizedlayer.size(), 0) << "Need visualized layer name for gradient.";

	// Set device id and mode
	if (FLAGS_gpu >= 0) {
		LOG(INFO) << "Use GPU with device ID " << FLAGS_gpu;
		Caffe::SetDevice(FLAGS_gpu);
		Caffe::set_mode(Caffe::GPU);
	} else {
		LOG(INFO) << "Use CPU.";
		Caffe::set_mode(Caffe::CPU);
	}
	// Instantiate the caffe net.
	Caffe::set_phase(Caffe::TEST);

	VisData* vd = locateDataAndVisualizedLayers();
	Net<float>& caffe_net = *vd->caffe_net;
	Blob<float>* dataBlob = vd->dataBlob;
	Blob<float>* visBlob = vd->visBlob;
	int visLayerid = vd->visLayerid;

	LOG(INFO) << "Forward...";

	vector<Blob<float>* > bottom_vec;
	float loss = 0;
	caffe_net.Forward(bottom_vec, &loss);
	const float* result_vec = visBlob->cpu_data();
	vector<int> cmaxs;
	vector<bool> istherecmax(visBlob->channels(), false);

	for (int n = 0; n < visBlob->num(); ++n) {
		for (int h = 0; h < visBlob->height(); ++h) {
			for (int w = 0; w < visBlob->width(); ++w) {
				int cmax = 0;
				float scoremax = -1000.0;
				for (int c = 0; c < visBlob->channels(); ++c) {
					const float score = result_vec[visBlob->offset(n, c, h, w)];
					if (score > scoremax) {
						scoremax = score;
						cmax = c;
					}
				}
				LOG(INFO) << "Max score (" << FLAGS_visualizedlayer << ")-n" << n << "-h" << h << "-w" << w << "= " << scoremax;
				LOG(INFO) << "Max channel (" << FLAGS_visualizedlayer << ")-n" << n << "-h" << h << "-w" << w << "= " << cmax;
				cmaxs.push_back(cmax);
				istherecmax[cmax] = true;
			}
		}
	}

	// Go through the input images and save for each n
	for (int n = 0; n < dataBlob->num(); ++n) {
		cv::Mat mat = caffe::ConvertBlobToCVMat(*dataBlob, true, n, FLAGS_datalayer_upscale, FLAGS_datalayer_mean_to_add);
		std::stringstream filenameStr;
		filenameStr << FLAGS_visdir << "/gradient-" << caffe_net.name() << "-n" << n << "-input.jpg";
		caffe::WriteImageFromCVMat(filenameStr.str(), mat);
	}

	float* visBlobVec = visBlob->mutable_cpu_diff();

	for (int c = 0; c < visBlob->channels(); ++c) {
		if (FLAGS_onlycmax && !istherecmax[c]) {
			continue;
		}
		for (int h = 0; h < visBlob->height(); ++h) {
			for (int w = 0; w < visBlob->width(); ++w) {
				// Initialize with zeros
				caffe::caffe_set(visBlob->count(), 0.0f, visBlobVec);
				LOG(INFO) << "Setting " << FLAGS_visualizedlayer << "-c" << c << "-h" << h << "-w" << w << " diff value to 1 for all n";
				for (int n = 0; n < visBlob->num(); ++n) {
					visBlobVec[visBlob->offset(n, c, h, w)] = 1;
				}

				LOG(INFO) << "Backward...";
				caffe_net.BackwardFrom(visLayerid);
				// Copy before doing transforms
				Blob<float> tmpblob;
				tmpblob.CopyFrom(*dataBlob, true, true);

				for (int n = 0; n < visBlob->num(); ++n) {
					if (FLAGS_onlycmax && c != cmaxs[n]) {
						continue;
					}
					float maxVal = -1000.0;
					float minVal = 1000.0;
					for (int ht = 0; ht < dataBlob->height(); ++ht) {
						for (int wt = 0; wt < dataBlob->width(); ++wt) {
							for (int ct = 0; ct < dataBlob->channels(); ++ct) {
								float val = dataBlob->cpu_diff()[dataBlob->offset(n, ct, ht, wt)];
								maxVal = std::max(val, maxVal);
								minVal = std::min(val, minVal);
							}
						}
					}
					// Normalize: map min to 0, max to 1
					for (int ht = 0; ht < dataBlob->height(); ++ht) {
						for (int wt = 0; wt < dataBlob->width(); ++wt) {
							for (int ct = 0; ct < dataBlob->channels(); ++ct) {
								float& val = dataBlob->mutable_cpu_diff()[dataBlob->offset(n, ct, ht, wt)];
								val = (val - minVal) / (maxVal - minVal) * FLAGS_gradient_upscale;
							}
						}
					}

					cv::Mat mat = caffe::ConvertBlobToCVMat(*dataBlob, false, n, 255.0, 0);
					std::stringstream filenameStr;
					filenameStr << FLAGS_visdir << "/gradient-" << caffe_net.name() <<
					            "-n" << n <<
					            "-" << FLAGS_visualizedlayer <<
					            "-c" << c <<
					            "-h" << h <<
					            "-w" << w << ".jpg";

					LOG(INFO) << "Saving gradient for " << FLAGS_visualizedlayer << "-n" << n << "-c" << c << "-h" << h << "-w" << w << " to " << filenameStr.str();
					caffe::WriteImageFromCVMat(filenameStr.str(), mat);
				}

				for (int n = 0; n < visBlob->num(); ++n) {
					if (FLAGS_onlycmax && c != cmaxs[n]) {
						continue;
					}

					// Put the max abs over the channels everywhere
					float maxVal = -1000.0;
					for (int ht = 0; ht < tmpblob.height(); ++ht) {
						for (int wt = 0; wt < tmpblob.width(); ++wt) {
							float chmax = 0;
							for (int ct = 0; ct < tmpblob.channels(); ++ct) {
								float val = std::abs(tmpblob.cpu_diff()[tmpblob.offset(n, ct, ht, wt)]);
								chmax = std::max(val, chmax);
								maxVal = std::max(val, maxVal);
							}
							for (int ct = 0; ct < tmpblob.channels(); ++ct) {
								tmpblob.mutable_cpu_diff()[tmpblob.offset(n, ct, ht, wt)] = chmax;
							}
						}
					}
					// Map max to 1.0
					for (int ht = 0; ht < tmpblob.height(); ++ht) {
						for (int wt = 0; wt < tmpblob.width(); ++wt) {
							for (int ct = 0; ct < tmpblob.channels(); ++ct) {
								float& val = tmpblob.mutable_cpu_diff()[tmpblob.offset(n, ct, ht, wt)];
								val /= maxVal;
								val *= FLAGS_saliency_upscale;
							}
						}
					}

					cv::Mat mat = caffe::ConvertBlobToCVMat(tmpblob, false, n, 255.0, 0);
					std::stringstream filenameStr;
					filenameStr << FLAGS_visdir << "/gradient-" << caffe_net.name() <<
					            "-n" << n <<
					            "-" << FLAGS_visualizedlayer <<
					            "-c" << c <<
					            "-h" << h <<
					            "-w" << w << "-saliency.jpg";

					LOG(INFO) << "Saving gradient (saliency) for " << FLAGS_visualizedlayer << "-n" << n << "-c" << c << "-h" << h << "-w" << w << " to " << filenameStr.str();
					caffe::WriteImageFromCVMat(filenameStr.str(), mat);
				}
			}
		}
	}

	return 0;
}
RegisterBrewFunction(gradient);

// classimage: compute a representative image for each class.
int classimage() {
	CHECK_GT(FLAGS_model.size(), 0) << "Need a model definition for classimage.";
	CHECK_GT(FLAGS_weights.size(), 0) << "Need model weights for classimage.";
	CHECK_GT(FLAGS_datalayer.size(), 0) << "Need data layer name for classimage.";
	CHECK_GT(FLAGS_visualizedlayer.size(), 0) << "Need visualized layer name for classimage.";

	// Set device id and mode
	if (FLAGS_gpu >= 0) {
		LOG(INFO) << "Use GPU with device ID " << FLAGS_gpu;
		Caffe::SetDevice(FLAGS_gpu);
		Caffe::set_mode(Caffe::GPU);
	} else {
		LOG(INFO) << "Use CPU.";
		Caffe::set_mode(Caffe::CPU);
	}
	// Instantiate the caffe net.
	Caffe::set_phase(Caffe::TEST);

	VisData* vd = locateDataAndVisualizedLayers();
	Net<float>& caffe_net = *vd->caffe_net;
	Blob<float>* dataBlob = vd->dataBlob;
	Blob<float>* visBlob = vd->visBlob;
	int visLayerid = vd->visLayerid;

	int itCount = FLAGS_iterations;
	float learning_rate = FLAGS_learningrate;
	float weight_decay = FLAGS_weightdecay;
	Blob<float>* labelBlob = new Blob<float>();
	labelBlob->ReshapeLike(*visBlob);

	for (int c = 0; c < visBlob->channels(); ++c) {
		for (int h = 0; h < visBlob->height(); ++h) {
			for (int w = 0; w < visBlob->width(); ++w) {
				caffe::caffe_set(labelBlob->count(), 0.0f, labelBlob->mutable_cpu_data());
				for (int n = 0; n < labelBlob->num(); ++n) {
					labelBlob->mutable_cpu_data()[labelBlob->offset(n, c, h, w)] = -1;
				}

				LOG(INFO) << "Initializing with mean image...";
				// TODO: MEAN IMAGE
				caffe::caffe_set(dataBlob->count(), 0.0f, dataBlob->mutable_cpu_data());
				// Go through the input images and save for each n
				for (int n = 0; n < dataBlob->num(); ++n) {
					cv::Mat mat = caffe::ConvertBlobToCVMat(*dataBlob, true, n, FLAGS_datalayer_upscale, FLAGS_datalayer_mean_to_add);
					std::stringstream filenameStr;
					filenameStr << FLAGS_visdir << "/classimage-" << caffe_net.name() << "-ait-n" << n << "-c" << c << ".jpg";

					LOG(INFO) << "Saving image: " << filenameStr.str();
					caffe::WriteImageFromCVMat(filenameStr.str(), mat);
				}

				for (int it = 0; it < itCount; ++it) {
					LOG(INFO) << "Forward...";
					vector<Blob<float>* > bottom_vec;
					float loss = 0;
					caffe_net.Forward(bottom_vec, &loss);

					// Compute loss
					caffe::caffe_copy(visBlob->count(),
							labelBlob->cpu_data(),
							visBlob->mutable_cpu_diff());

					LOG(INFO) << "Backward...";
					caffe_net.BackwardFrom(visLayerid);

					// L2 regularization, update the diff
					caffe::caffe_axpy(dataBlob->count(),
					           weight_decay,
					           dataBlob->cpu_data(),
					           dataBlob->mutable_cpu_diff());

					// Update the image using the computed gradient
					caffe::caffe_axpy(dataBlob->count(),
					           (-1)*learning_rate,
					           dataBlob->cpu_diff(),
					           dataBlob->mutable_cpu_data());

					LOG(INFO) << "Updated image data sum: " << dataBlob->asum_data();

					if (it % 100 == 0) {
						// Go through the input images and save for each n
						for (int n = 0; n < dataBlob->num(); ++n) {
							cv::Mat mat = caffe::ConvertBlobToCVMat(*dataBlob, true, n, FLAGS_datalayer_upscale, FLAGS_datalayer_mean_to_add);
							std::stringstream filenameStr;
							filenameStr << FLAGS_visdir << "/classimage-" << caffe_net.name() << "-it" << it << "-n" << n << "-c" << c <<  ".jpg";
							LOG(INFO) << "Saving image: " << filenameStr.str();
							caffe::WriteImageFromCVMat(filenameStr.str(), mat);
						}
					}
				}
			}
		}
	}
	// Go through the input images and save for each n
	for (int n = 0; n < dataBlob->num(); ++n) {
		cv::Mat mat = caffe::ConvertBlobToCVMat(*dataBlob, true, n, FLAGS_datalayer_upscale, FLAGS_datalayer_mean_to_add);
		std::stringstream filenameStr;
		filenameStr << FLAGS_visdir << "/classimage-" << caffe_net.name() << "-n" << n << ".jpg";
		LOG(INFO) << "Saving image: " << filenameStr.str();
		caffe::WriteImageFromCVMat(filenameStr.str(), mat);
	}

	return 0;
}
RegisterBrewFunction(classimage);

// Time: benchmark the execution time of a model.
int time() {
	CHECK_GT(FLAGS_model.size(), 0) << "Need a model definition to time.";

	// Set device id and mode
	if (FLAGS_gpu >= 0) {
		LOG(INFO) << "Use GPU with device ID " << FLAGS_gpu;
		Caffe::SetDevice(FLAGS_gpu);
		Caffe::set_mode(Caffe::GPU);
	} else {
		LOG(INFO) << "Use CPU.";
		Caffe::set_mode(Caffe::CPU);
	}
	// Instantiate the caffe net.
	Caffe::set_phase(Caffe::TRAIN);
	Net<float> caffe_net(FLAGS_model);

	// Do a clean forward and backward pass, so that memory allocation are done
	// and future iterations will be more stable.
	LOG(INFO) << "Performing Forward";
	// Note that for the speed benchmark, we will assume that the network does
	// not take any input blobs.
	float initial_loss;
	caffe_net.Forward(vector<Blob<float>*>(), &initial_loss);
	LOG(INFO) << "Initial loss: " << initial_loss;
	LOG(INFO) << "Performing Backward";
	caffe_net.Backward();

	const vector<shared_ptr<Layer<float> > >& layers = caffe_net.layers();
	vector<vector<Blob<float>*> >& bottom_vecs = caffe_net.bottom_vecs();
	vector<vector<Blob<float>*> >& top_vecs = caffe_net.top_vecs();
	const vector<vector<bool> >& bottom_need_backward =
	    caffe_net.bottom_need_backward();
	LOG(INFO) << "*** Benchmark begins ***";
	LOG(INFO) << "Testing for " << FLAGS_iterations << " iterations.";
	Timer total_timer;
	total_timer.Start();
	Timer forward_timer;
	Timer backward_timer;
	Timer timer;
	std::vector<double> forward_time_per_layer(layers.size(), 0.0);
	std::vector<double> backward_time_per_layer(layers.size(), 0.0);
	double forward_time = 0.0;
	double backward_time = 0.0;
	for (int j = 0; j < FLAGS_iterations; ++j) {
		Timer iter_timer;
		iter_timer.Start();
		forward_timer.Start();
		for (int i = 0; i < layers.size(); ++i) {
			timer.Start();
			// Although Reshape should be essentially free, we include it here
			// so that we will notice Reshape performance bugs.
			layers[i]->Reshape(bottom_vecs[i], top_vecs[i]);
			layers[i]->Forward(bottom_vecs[i], top_vecs[i]);
			forward_time_per_layer[i] += timer.MicroSeconds();
		}
		forward_time += forward_timer.MicroSeconds();
		backward_timer.Start();
		for (int i = layers.size() - 1; i >= 0; --i) {
			timer.Start();
			layers[i]->Backward(top_vecs[i], bottom_need_backward[i],
			                    bottom_vecs[i]);
			backward_time_per_layer[i] += timer.MicroSeconds();
		}
		backward_time += backward_timer.MicroSeconds();
		LOG(INFO) << "Iteration: " << j + 1 << " forward-backward time: "
		          << iter_timer.MilliSeconds() << " ms.";
	}
	LOG(INFO) << "Average time per layer: ";
	for (int i = 0; i < layers.size(); ++i) {
		const caffe::string& layername = layers[i]->layer_param().name();
		LOG(INFO) << std::setfill(' ') << std::setw(10) << layername <<
		          "\tforward: " << forward_time_per_layer[i] / 1000 /
		          FLAGS_iterations << " ms.";
		LOG(INFO) << std::setfill(' ') << std::setw(10) << layername  <<
		          "\tbackward: " << backward_time_per_layer[i] / 1000 /
		          FLAGS_iterations << " ms.";
	}
	total_timer.Stop();
	LOG(INFO) << "Average Forward pass: " << forward_time / 1000 /
	          FLAGS_iterations << " ms.";
	LOG(INFO) << "Average Backward pass: " << backward_time / 1000 /
	          FLAGS_iterations << " ms.";
	LOG(INFO) << "Average Forward-Backward: " << total_timer.MilliSeconds() /
	          FLAGS_iterations << " ms.";
	LOG(INFO) << "Total Time: " << total_timer.MilliSeconds() << " ms.";
	LOG(INFO) << "*** Benchmark ends ***";
	return 0;
}
RegisterBrewFunction(time);

int main(int argc, char** argv) {
	// Print output to stderr (while still logging).
	FLAGS_alsologtostderr = 1;
	// Usage message.
	gflags::SetUsageMessage("command line brew\n"
	                        "usage: caffe <command> <args>\n\n"
	                        "commands:\n"
	                        "  train           train or finetune a model\n"
	                        "  test            score a model\n"
	                        "  gradient       visualize the gradient of a model\n"
	                        "  classimage       compute representative class images descent for a model using gradient\n"
	                        "  device_query    show GPU diagnostic information\n"
	                        "  time            benchmark model execution time");
	// Run tool or show usage.
	caffe::GlobalInit(&argc, &argv);
	if (argc == 2) {
		return GetBrewFunction(caffe::string(argv[1]))();
	} else {
		gflags::ShowUsageWithFlagsRestrict(argv[0], "tools/caffe");
	}
}
