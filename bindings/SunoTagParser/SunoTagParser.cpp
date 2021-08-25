//
// Created by Arjun Phull on 2021-07-05.
//

#include <unistd.h>
#include <algorithm>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <sstream>
#include <fstream>
#include <queue>
#include <string>
#include <fileref.h>
#include <tfile.h>
#include <tag.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <id3v2tag.h>
#include <xiphcomment.h>
#include <tagunion.h>
#include <id3v2frame.h>
#include <mp4item.h>
#include <mpeg/id3v2/frames/attachedpictureframe.h>
#include <mp4/mp4tag.h>
#include <jni.h>
#include <toolkit/tfilestream.h>

#define NUM_THREADS 2
#define FINISHED "~"
#define DELIMITER "|*|"

using namespace TagLib;
using namespace std;

bool stringEndsWith(string const &value, string const &ending)
{
    if (ending.size() > value.size()) {
        return false;
    }
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

bool isAudioFileExt(string &ext) {
    transform(ext.begin(), ext.end(), ext.begin(), ::toupper);
    if (ext == "MP3" ||
        ext == "OGG" ||
        ext == "FLAC" ||
        ext == "MPC" ||
        ext == "WV" ||
        ext == "SPX" ||
        ext == "OPUS" ||
        ext == "TTA" ||
        ext == "M4A" || ext == "M4R" || ext == "M4B" || ext == "M4P" || ext == "MP4" || ext == "3G2" || ext == "M4V" ||
        ext == "WMA" || ext == "ASF" ||
        ext == "AIF" || ext == "AIFF" || ext == "AFC" || ext == "AIFC" ||
        ext == "WAV" ||
        ext == "APE" ||
        ext == "MOD" ||
        ext == "S3M" ||
        ext == "IT" ||
        ext == "XM") {
        return true;
    }

    return false;
}

void inspectFile(int fileDescriptor, vector<string> &tagBuffer) {
    ostringstream info;
    info << "FD=" << fileDescriptor;

    FileStream fileStream(dup(fileDescriptor), true);
    FileRef file(&fileStream, true, AudioProperties::ReadStyle::Accurate);
    if (file.isNull()) {
        // this conditional can be triggered by audio files with bad headers. If it looks like and audio file, indicate such.
        char fdPath[256];
        char filePath[PATH_MAX];
        sprintf(fdPath, "/proc/self/fd/%d", fileDescriptor);
        int numBytes = readlink(fdPath, filePath, PATH_MAX);
        if (numBytes != -1) {
            string filename(filePath, numBytes);
            auto index = filename.rfind('.');
            if(index != string::npos) {
                string extension = filename.substr(index + 1);
                if (isAudioFileExt(extension)) {
                    tagBuffer.push_back(info.str());
                }
            }
        }
        return;
    }

    Tag *tag = file.tag();
    if (!tag) {
        return;
    }

    info << DELIMITER
         << "ARTIST=" << tag->artist() << DELIMITER
         << "ALBUM=" << tag->album() << DELIMITER
         << "TITLE=" << tag->title() << DELIMITER
         << "TRACK=" << tag->track();

    /* many mp4/m4a/m4b files do not specify duration correctly in the trak and/or mdhd boxes,
       the latter which is check by TagLib (I checked the former manually).
       Consequently, the bitrate will be absurdly high.
       Flag these files by not returning length (the android media player calculates their durations correctly) */
    AudioProperties *properties = file.audioProperties();
    if (properties->bitrate() < 1000 && properties->lengthInMilliseconds() > 0) {
        info << DELIMITER << "LENGTH=" << properties->lengthInMilliseconds();
    }
    tagBuffer.push_back(info.str());
}

void awaitTask(atomic_bool &terminate,
               mutex &workMutex,
               condition_variable &workCondVar,
               mutex &doneWorkMutex,
               condition_variable &doneWorkCondVar,
               int &workSignal,
               fstream &pipe,
               queue<int> &fdQueue,
               mutex &pipeMutex,
               mutex &fdQueueMutex) {
    vector<string> tagBuffer;
    while (!terminate) {
        // wait until there's work to do
        unique_lock<mutex> lock(workMutex);
        while (!terminate && workSignal == 0) {
            workCondVar.wait(lock);
        }

        if (terminate) {
            break;
        }

        // pull file descriptor off the queue
        int fd;
        bool queueEmpty = true;
        {
            lock_guard<mutex> queueLock(fdQueueMutex);
            if (!fdQueue.empty()) {
                queueEmpty = false;
                fd = fdQueue.front();
                fdQueue.pop();
            }
        }

        if (queueEmpty) {
            if (!tagBuffer.empty()) {
                lock_guard<mutex> pipeLock(pipeMutex);
                for (string const &s : tagBuffer) {
                    pipe << s << endl;
                }
                tagBuffer.clear();
            }

            // wake up pipe reader
            workSignal = 0;
            doneWorkCondVar.notify_all();
        } else {
            // handle the file descriptor
            inspectFile(fd, tagBuffer);

            // try to push data through the pipe; if unavailable, continue to append buffer
            unique_lock<mutex> pipeLock(pipeMutex, try_to_lock);
            if (pipeLock.owns_lock()) {
                for (string const &s : tagBuffer) {
                    pipe << s << endl;
                }
                pipeLock.unlock();
                tagBuffer.clear();
            }
        }
    }
}

extern "C"
JNIEXPORT jbyteArray JNICALL
Java_com_github_arjunphull_sunoaudiobookplayer_file_TagParser_getCoverArt(JNIEnv *env, jclass jClass, jint fileDescriptor) {
    FileStream fileStream(dup(fileDescriptor), true);
    FileRef file(&fileStream, false, AudioProperties::ReadStyle::Fast);
    Tag *tag = file.tag();

    jbyte *coverArtData;
    unsigned int coverArtDataSize = 0;
    if (auto *tu = dynamic_cast<TagUnion *>(tag)) {
        if (auto *t = dynamic_cast<ID3v2::Tag *>(tu->tag(0))) {
            ID3v2::FrameList l = t->frameList("APIC");
            if (!l.isEmpty()) {
                if (auto *f = dynamic_cast<TagLib::ID3v2::AttachedPictureFrame *>(l.front())) {
                    coverArtDataSize = f->picture().size();
                    coverArtData = reinterpret_cast<jbyte *>(f->picture().data());
                }
            }
        } else if (auto *t = dynamic_cast<Ogg::XiphComment *>(tu->tag(0))) {
            List<FLAC::Picture *> l = t->pictureList();
            if (!l.isEmpty()) {
                coverArtDataSize = l.front()->data().size();
                coverArtData = reinterpret_cast<jbyte *>(l.front()->data().data());
            }
        }
    } else if (auto *t = dynamic_cast<MP4::Tag *>(tag)) {
        MP4::ItemMap itemMap = t->itemMap();
        MP4::Item coverItem = itemMap["covr"];
        MP4::CoverArtList coverArtList = coverItem.toCoverArtList();
        if (!coverArtList.isEmpty()) {
            MP4::CoverArt coverArt = coverArtList.front();
            coverArtDataSize = coverArt.data().size();
            coverArtData = reinterpret_cast<jbyte *>(coverArt.data().data());
        }
    }

    jbyteArray ret = env->NewByteArray(coverArtDataSize);
    env->SetByteArrayRegion(ret, 0, coverArtDataSize, coverArtData);
    return ret;
}

extern "C"
JNIEXPORT jstring JNICALL
Java_com_github_arjunphull_sunoaudiobookplayer_file_TagParser_getTagInfo(JNIEnv *env, jobject jobj, jstring args) {
    string argString = env->GetStringUTFChars(args, nullptr);
    string namedPipePath;
    fstream pipe;
    queue<int> fdQueue;
    mutex pipeMutex;
    mutex fdQueueMutex;
    vector<thread> threads;
    vector<mutex> threadWorkMutexes(NUM_THREADS);
    vector<condition_variable> threadWorkConditionsVars(NUM_THREADS);
    vector<int> threadWorkSignals(NUM_THREADS);
    vector<mutex> threadDoneWorkMutexes(NUM_THREADS);
    vector<condition_variable> threadDoneWorkCondVars(NUM_THREADS);
    atomic_bool terminateThreads(false);

    // start threads
    for (int i = 0; i < NUM_THREADS; i++) {
        threadWorkSignals[i] = 0;
        threads.emplace_back(thread(awaitTask,
                                    ref(terminateThreads),
                                    ref(threadWorkMutexes[i]),
                                    ref(threadWorkConditionsVars[i]),
                                    ref(threadDoneWorkMutexes[i]),
                                    ref(threadDoneWorkCondVars[i]),
                                    ref(threadWorkSignals[i]),
                                    ref(pipe),
                                    ref(fdQueue),
                                    ref(pipeMutex),
                                    ref(fdQueueMutex)));
    }

    // parse out pipe path and dir path
    size_t pos;
    string token;
    while ((pos = argString.find('\n')) != string::npos) {
        token = argString.substr(0, pos);
        if (token.rfind("pipe=", 0) == 0) {
            namedPipePath = token.substr(5);
        }
        argString.erase(0, pos + 1);
    }

    //check to see if we can access the named pipe
    if (access(namedPipePath.c_str(), (R_OK | W_OK)) != 0) {
        //if it doesn't exist, create it
        if (errno != ENOENT) {
            return env->NewStringUTF("can't access named pipe");
        }
        if (mkfifo(namedPipePath.c_str(), 0666) != 0) {
            return env->NewStringUTF("can't create named pipe");
        }
    }

    while (true) {
        // read file descriptors off the pipe
        bool done = false;
        string input;
        pipe.open(namedPipePath, fstream::in);
        pipe >> input;
        pipe.close();
        if (stringEndsWith(input, FINISHED)) {
            done = true;
            input.erase(input.end() - char_traits<char>::length(FINISHED), input.end());
        }

        if (!input.empty()) {
            // push file descriptors into the queue
            {
                lock_guard<mutex> fdQueueLock(fdQueueMutex);
                istringstream iss(input);
                for (int fd; iss >> fd;) {
                    fdQueue.push(fd);
                    if (iss.peek() == ',') {
                        iss.ignore();
                    }
                }
            }

            // wake threads up
            pipe.open(namedPipePath, fstream::out | fstream::app);
            for (int i = 0; i < NUM_THREADS; i++) {
                lock_guard<mutex> workLock(threadWorkMutexes[i]);
                threadWorkSignals[i] = 1;
                threadWorkConditionsVars[i].notify_all();
            }

            // wait until threads are done their work
            for (int i = 0; i < NUM_THREADS; i++) {
                unique_lock<mutex> workDoneLock(threadDoneWorkMutexes[i]);
                while (threadWorkSignals[i]) {
                    threadDoneWorkCondVars[i].wait(workDoneLock);
                }
            }
            pipe << FINISHED << std::endl;
            pipe.close();
        }

        if (done) {
            terminateThreads = true;
            for (int i = 0; i < NUM_THREADS; i++) {
                lock_guard<mutex> workLock(threadWorkMutexes[i]);
                threadWorkConditionsVars[i].notify_all();
            }
            for (auto &t : threads) {
                t.join();
            }
            break;
        }
    }

    return env->NewStringUTF("");
}
