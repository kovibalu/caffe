import os
import sys
import math

from lib.intrinsic import intrinsic
from lib.intrinsic import comparison

from lib.utils.misc.pathresolver import acrp
from lib.utils.data import fileproc

# 0 mitintrinsic
# 1 Sean's synthetic dataset
# 2 IIW dense
DATASETCHOICE = 2

SAVEROOTDIR = acrp('experiments/mitintrinsic/allresults')
#IIWTAGPATH = acrp('data/iiw-dataset/denseimages.txt')
IIWTAGPATH = acrp('data/iiw-dataset/all-images.txt')
JOBSFILEPATH = acrp('data/iiw-dataset/jobs-bin-threshold-all.txt')

# The following objects were used in the evaluation. For the learning algorithms
# (not included here), we used two-fold cross-validation with the following
# randomly chosen split.
SET1MIT = ['box', 'cup1', 'cup2', 'dinosaur', 'panther', 'squirrel', 'sun', 'teabag2']
SET2MIT = ['deer', 'frog1', 'frog2', 'paper1', 'paper2', 'raccoon', 'teabag1', 'turtle']

SETINDOOR = map(lambda n: str(n), range(1, 25))

SETIIW = fileproc.freadlines(IIWTAGPATH)

if DATASETCHOICE == 0:
    ALL_TAGS = SET1MIT + SET2MIT
    ERRORMETRIC = 0  # LMSE
elif DATASETCHOICE == 1:
    ALL_TAGS = SETINDOOR
    ERRORMETRIC = 0  # LMSE
elif DATASETCHOICE == 2:
    ALL_TAGS = SETIIW
    #ALL_TAGS = ['100520', '101684', '76601', '101880', '76078', '104535', '34047']
    ALL_TAGS = ['117613', '117832', '71129', '93481', '93835']
    ERRORMETRIC = 1  # WHDR
else:
    raise ValueError('Unknown dataset choice: {0}'.format(DATASETCHOICE))

# The following four objects weren't used in the evaluation because they have
# slight problems, but you may still find them useful.
EXTRA_TAGS = ['apple', 'pear', 'phone', 'potato']

# Use L1 to compute the final results. (For efficiency, the parameters are still
# tuned using least squares.)
USE_L1 = False

# Output of the algorithms will be saved here
if USE_L1:
    RESULTS_DIR = os.path.join(SAVEROOTDIR, 'results_L1')
else:
    RESULTS_DIR = os.path.join(SAVEROOTDIR, 'results')

ESTIMATORS = [
                #('Baseline (BAS)', intrinsic.BaselineEstimator),
                #('Grayscale Retinex with CNN predicted threshold images using RGB images', intrinsic.GrayscaleRetinexWithThresholdImageRGBEstimator),
                #('Grayscale Retinex with CNN predicted threshold images using chromaticity + grayscale image, small network 3 conv layers', intrinsic.GrayscaleRetinexWithThresholdImageChromSmallNetEstimator),
                #('Grayscale Retinex with CNN predicted threshold images using chromaticity + grayscale image, big network 4 conv layers', intrinsic.GrayscaleRetinexWithThresholdImageChromBigNetEstimator),
                #('Grayscale Retinex with CNN predicted threshold images using chromaticity + grayscale image, big network 4 conv layers, concatenated conv1+3 output', intrinsic.GrayscaleRetinexWithThresholdImageChromBigNetConcatEstimator),
                #('Grayscale Retinex with CNN predicted threshold images using chromaticity + grayscale image, big network 4 conv layers, concatenated conv1+3 output + maxpool between conv1-2 and 2-3', intrinsic.GrayscaleRetinexWithThresholdImageChromBigNetConcatMaxpoolEstimator),
                #('Grayscale Retinex with ground truth threshold images', intrinsic.GrayscaleRetinexWithThresholdImageGroundTruthEstimator),
                ('Zhao2012', intrinsic.Zhao2012Estimator),
                #('Zhao2012 with ground truth reflectance groups', intrinsic.Zhao2012GroundTruthGroupsEstimator),
                #('Grayscale Retinex (GR-RET)', intrinsic.GrayscaleRetinexEstimator),
                #('Color Retinex (COL-RET)', intrinsic.ColorRetinexEstimator),
                #("Weiss's Algorithm (W)", intrinsic.WeissEstimator),
                #('Weiss + Retinex (W+RET)', intrinsic.WeissRetinexEstimator),
                ]

lines = fileproc.freadlines(JOBSFILEPATH)
JOB_PARAMS = []
for l in lines:
    tokens = l.split(' ')
    tag, threshold_chrom, classnum, samplenum, shift = tokens
    params = {'threshold_chrom': math.exp(float(threshold_chrom) + float(shift))}

    job_param = {'EstimatorClass': intrinsic.Zhao2012Estimator, 'params': params, 'tag': tag, 'classnum': classnum, 'samplenum': samplenum}
    JOB_PARAMS.append(job_param)

PROCESSED_TASKS_PATH = os.path.join(RESULTS_DIR, 'proctasks.txt')

if __name__ == '__main__':
    if len(sys.argv) < 2 or sys.argv[1] not in ['dispatch', 'aggregate', 'simple', 'dispatchpredefjobs', 'aggregatepredefjobs']:
        print 'Usage: run_comparison.py dispatch|aggregate|simple|dispatchpredefjobs|aggregatepredefjobs RERUNALLTASKS=True|False?'
        sys.exit(1)

    ORACLEEACHIMAGE = True
    USESAVEDSCORES = False
    PARTIALRESULTS = False
    IMAGESFORALLPARAMS = True
    option = sys.argv[1]
    if len(sys.argv) >= 3:
        RERUNALLTASKS = {'True': True, 'False': False}.get(sys.argv[2])
        if RERUNALLTASKS is None:
            raise ValueError('Invalid input argument for RERUNALLTASKS: {0}'.format(sys.argv[2]))
    else:
        RERUNALLTASKS = False

    if option == 'dispatch':
        comparison.dispatch_comparison_experiment(DATASETCHOICE, ALL_TAGS, ERRORMETRIC, USE_L1, RESULTS_DIR, ESTIMATORS, RERUNALLTASKS)
    elif option == 'aggregate':
        comparison.aggregate_comparison_experiment(DATASETCHOICE, ALL_TAGS, ERRORMETRIC, USE_L1, RESULTS_DIR, ESTIMATORS, USESAVEDSCORES, ORACLEEACHIMAGE, PARTIALRESULTS)
    elif option == 'simple':
        comparison.run_experiment(DATASETCHOICE, ALL_TAGS, ERRORMETRIC, USE_L1, RESULTS_DIR, ESTIMATORS, ORACLEEACHIMAGE, IMAGESFORALLPARAMS)
    elif option == 'dispatchpredefjobs':
        comparison.dispatch_predefined_jobs(JOB_PARAMS, DATASETCHOICE, ERRORMETRIC, USE_L1, RESULTS_DIR, RERUNALLTASKS, PROCESSED_TASKS_PATH)
    elif option == 'aggregatepredefjobs':
        comparison.aggregate_predifined_jobs(JOB_PARAMS, RESULTS_DIR, PROCESSED_TASKS_PATH)
    else:
        raise ValueError('Invalid option: {0}'.format(option))
