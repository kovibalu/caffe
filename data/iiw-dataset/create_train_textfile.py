import os
from os import listdir
import sys
import json
import random
import numpy as np

sys.path.append('data')
import common

BIGGERDIMSIZE = 1100
USESIMPLEGAMMAFORSAVE = True

# this script finds all the dense images and writes their name to a file
origpath = 'data/iiw-dataset/data'

origdirnames = listdir(origpath)
# filter for only json files
origdirnames = [x for x in origdirnames if os.path.splitext(x)[1] == '.json']
origdirnames.sort()

random.seed(42)
# Select 20% test set randomly
testset_size = int(len(origdirnames) * 0.2)
trainset_size = len(origdirnames) - testset_size
testset = random.sample(origdirnames, testset_size)

f_train = open('data/iiw-dataset/train.txt', 'w')
f_test = open('data/iiw-dataset/test.txt', 'w')

equal_cmp_train = []
notequal_cmp_train = []
equal_cmp_test = []
notequal_cmp_test = []

for filename in origdirnames:
    print 'Processing file {0}...'.format(filename)

    filepath = os.path.join(origpath, filename)
    trunc_filename, ext = os.path.splitext(filename)
    trunc_filepath, _ = os.path.splitext(filepath)

    # load image, compute grayscale + chromaticity images
    linimg = common.load_image(trunc_filepath + '.png', is_srgb=True)
    linimg = common.resize_and_crop_image(linimg, resize=BIGGERDIMSIZE, crop=None, keep_aspect_ratio=True)
    grayimg = np.mean(linimg, axis=2)
    chromimg = common.compute_chromaticity_image(linimg)

    # gamma correction
    if USESIMPLEGAMMAFORSAVE:
        grayimg = grayimg ** (1. / 2.2)
        chromimg = chromimg ** (1. / 2.2)
    else:
        grayimg = common.rgb_to_srgb(grayimg)
        chromimg = common.rgb_to_srgb(chromimg)

    grayimg_path = trunc_filepath + '-gray.png'
    chromimg_path = trunc_filepath + '-chrom.png'
    common.save_image(grayimg_path, grayimg, is_srgb=False)
    common.save_image(chromimg_path, chromimg, is_srgb=False)

    judgements = json.load(open(filepath))
    points = judgements['intrinsic_points']
    comparisons = judgements['intrinsic_comparisons']
    id_to_points = {p['id']: p for p in points}

    if not points:
        continue

    if filename in testset:
        equal_cmp = equal_cmp_test
        notequal_cmp = notequal_cmp_test
    else:
        equal_cmp = equal_cmp_train
        notequal_cmp = notequal_cmp_train

    for c in comparisons:
        # "darker" is "J_i" in our paper
        darker = c['darker']
        if darker not in ('1', '2', 'E'):
            continue

        # "darker_score" is "w_i" in our paper
        weight = c['darker_score']
        if weight <= 0 or weight is None:
            continue

        point1 = id_to_points[c['point1']]
        point2 = id_to_points[c['point2']]
        if not point1['opaque'] or not point2['opaque']:
            continue

        if darker == 'E':
            equal_cmp.append((trunc_filename, point1['x'], point1['y'], point2['x'], point2['y']))
        else:
            notequal_cmp.append((trunc_filename, point1['x'], point1['y'], point2['x'], point2['y']))

print 'Saving gathered info to training and testing files...'

for f, equal_cmp, notequal_cmp in [(f_test, equal_cmp_test, notequal_cmp_test), (f_train, equal_cmp_train, notequal_cmp_train)]:
    print 'Number of equal comparisons found: {0}'.format(len(equal_cmp))
    print 'Number of notequal comparisons found: {0}'.format(len(notequal_cmp))
    for i, c in enumerate(equal_cmp):
        # for every positive example, we put a negative example too
        trunc_filename, p1x, p1y, p2x, p2y = c
        grayimg_path = os.path.join(origpath, trunc_filename) + '-gray.png'
        chromimg_path = os.path.join(origpath, trunc_filename) + '-chrom.png'
        f.write('{0} {1} {2} {3} {4} {5}\n'.format(grayimg_path, chromimg_path, p1x, p1y, p2x, p2y))

        c = notequal_cmp[i % len(notequal_cmp)]
        trunc_filename, p1x, p1y, p2x, p2y = c
        grayimg_path = os.path.join(origpath, trunc_filename) + '-gray.png'
        chromimg_path = os.path.join(origpath, trunc_filename) + '-chrom.png'
        f.write('{0} {1} {2} {3} {4} {5}\n'.format(grayimg_path, chromimg_path, p1x, p1y, p2x, p2y))

f_train.close()
f_test.close()

print 'Done.'







