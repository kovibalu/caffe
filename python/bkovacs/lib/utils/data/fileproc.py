import threading
from Queue import Queue


def freadlines(filepath, strip=True):
    with open(filepath, 'r') as f:
        if strip:
            lines = [s.strip() for s in f.readlines()]
        else:
            lines = f.readlines()

    return lines


def fwritelines(filepath, lines, endline=True):
    with open(filepath, 'w') as f:
        for l in lines:
            if endline:
                l = '{0}\n'.format(l)

            f.write(l)


class AsynchronousFileReader(threading.Thread):
    '''
    Helper class to implement asynchronous reading of a file
    in a separate thread. Pushes read lines on a queue to
    be consumed in another thread.
    '''

    def __init__(self, fd, queue):
        assert isinstance(queue, Queue)
        assert callable(fd.readline)
        threading.Thread.__init__(self)
        self._fd = fd
        self._queue = queue

    def run(self):
        '''The body of the tread: read lines and put them on the queue.'''
        for line in iter(self._fd.readline, ''):
            self._queue.put(line)

    def eof(self):
        '''Check whether there is no more content to expect.'''
        return not self.is_alive() and self._queue.empty()
