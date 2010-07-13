/**
@file	 AsyncAudioDeviceAlsa.h
@brief   Handle Alsa audio devices
@author  Steve / DH1DM, Tobias Blomberg / SM0SVX
@date	 2009-07-21

Implements the low level interface to an Alsa audio device.

\verbatim
Async - A library for programming event driven applications
Copyright (C) 2003-2009 Tobias Blomberg / SM0SVX

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
\endverbatim
*/



/****************************************************************************
 *
 * System Includes
 *
 ****************************************************************************/

#include <sigc++/sigc++.h>
#include <poll.h>
#include <iostream>
#include <cmath>


/****************************************************************************
 *
 * Project Includes
 *
 ****************************************************************************/

#include <AsyncFdWatch.h>


/****************************************************************************
 *
 * Local Includes
 *
 ****************************************************************************/

#include "AsyncAudioDeviceAlsa.h"
#include "AsyncAudioDeviceFactory.h"



/****************************************************************************
 *
 * Namespaces to use
 *
 ****************************************************************************/

using namespace std;
using namespace Async;
using namespace SigC;



/****************************************************************************
 *
 * Defines & typedefs
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Local class definitions
 *
 ****************************************************************************/

class AudioDeviceAlsa::AlsaWatch : public SigC::Object
{
  public:
    AlsaWatch(pollfd *pfds, int nfds)
    {
      for (int i = 0; i < nfds; i++)
      {
        if (pfds[i].events & POLLOUT)
        {
          FdWatch *watch = new FdWatch(pfds[i].fd, FdWatch::FD_WATCH_WR);
          watch->activity.connect(slot(*this, &AlsaWatch::writeEvent));
          watch_list.push_back(watch);
        }
        if (pfds[i].events & POLLIN)
        {
          FdWatch *watch = new FdWatch(pfds[i].fd, FdWatch::FD_WATCH_RD);
          watch->activity.connect(slot(*this, &AlsaWatch::readEvent));
          watch_list.push_back(watch);
        }
        pfd_map[pfds[i].fd] = pfds[i];
      }
    }
  
    ~AlsaWatch()
    {
      std::list<FdWatch*>::const_iterator cii;
      for(cii = watch_list.begin(); cii != watch_list.end(); ++cii)
      {
        delete *cii;
      }
    }
    
    void setEnabled(bool enable)
    {
      std::list<FdWatch*>::const_iterator cii;
      for(cii = watch_list.begin(); cii != watch_list.end(); ++cii)
      {
        (*cii)->setEnabled(enable);
      }
    }
  
    SigC::Signal2<void, FdWatch*, pollfd*> activity;

  private:
    std::map<int, pollfd> pfd_map;
    std::list<FdWatch*> watch_list;

    void writeEvent(FdWatch *watch)
    {
      pollfd pfd = pfd_map[watch->fd()];
      pfd.revents = POLLOUT;
      activity(watch, &pfd);
    }
    void readEvent(FdWatch *watch)
    {
      pollfd pfd = pfd_map[watch->fd()];
      pfd.revents = POLLIN;
      activity(watch, &pfd);
    }
};


/****************************************************************************
 *
 * Prototypes
 *
 ****************************************************************************/



/****************************************************************************
 *
 * Exported Global Variables
 *
 ****************************************************************************/




/****************************************************************************
 *
 * Local Global Variables
 *
 ****************************************************************************/

REGISTER_AUDIO_DEVICE_TYPE("alsa", AudioDeviceAlsa);


/****************************************************************************
 *
 * Public member functions
 *
 ****************************************************************************/

AudioDeviceAlsa::AudioDeviceAlsa(const std::string& dev_name)
  : AudioDevice(dev_name), play_handle(0), rec_handle(0), play_watch(0),
    rec_watch(0), duplex(false)
{
  snd_pcm_t *play, *capture;

    // Open the device to check its duplex capability
  if (snd_pcm_open(&play, dev_name.c_str(), SND_PCM_STREAM_PLAYBACK, 0) == 0)
  {
     // Further initialization is not required here, since the AudioDevice
     // creator function will open the audio device again to check the assigned
     // I/O parameters.
    if (snd_pcm_open(&capture, dev_name.c_str(), 
                     SND_PCM_STREAM_CAPTURE, 0) == 0)
    {
      snd_pcm_close(capture);
      duplex = true;
    }
    snd_pcm_close(play);
  }
} /* AudioDeviceAlsa::AudioDeviceAlsa */


AudioDeviceAlsa::~AudioDeviceAlsa(void)
{
  
} /* AudioDeviceAlsa::~AudioDeviceAlsa */


int AudioDeviceAlsa::blocksize(void)
{
  return block_size;
} /* AudioDeviceAlsa::blocksize */


bool AudioDeviceAlsa::isFullDuplexCapable(void)
{
  return duplex;
} /* AudioDeviceAlsa::isFullDuplexCapable */


void AudioDeviceAlsa::audioToWriteAvailable(void)
{
  //printf("AudioDeviceAlsa::audioToWriteAvailable\n");
  if (play_watch)
  {
    play_watch->setEnabled(true);
  }
} /* AudioDeviceAlsa::audioToWriteAvailable */


void AudioDeviceAlsa::flushSamples(void)
{
  if (play_watch)
  {
    play_watch->setEnabled(true);
  }  
} /* AudioDeviceAlsa::flushSamples */


int AudioDeviceAlsa::samplesToWrite(void) const
{
  if ((mode() != MODE_WR) && (mode() != MODE_RDWR))
  {
    return 0;
  }

  int space_avail = snd_pcm_avail_update(play_handle);
  return (space_avail < 0) ?
            0 : (block_count * block_size) - space_avail;

} /* AudioDeviceAlsa::samplesToWrite */



/****************************************************************************
 *
 * Protected member functions
 *
 ****************************************************************************/

bool AudioDeviceAlsa::openDevice(Mode mode)
{
  closeDevice();

  if ((mode == MODE_WR) || (mode == MODE_RDWR))
  {
    if (snd_pcm_open(&play_handle, dev_name.c_str(),
                     SND_PCM_STREAM_PLAYBACK, 0) < 0)
    {
      cerr << "*** ERROR: Open playback audio device failed" << endl;
      return false;
    }

    if (!initParams(play_handle))
    {
      closeDevice();
      return false;
    }

    int play_nfds = snd_pcm_poll_descriptors_count(play_handle);

    pollfd play_pfds[play_nfds];
    snd_pcm_poll_descriptors(play_handle, play_pfds, play_nfds);

    play_watch = new AlsaWatch(play_pfds, play_nfds);
    play_watch->activity.connect(
            slot(*this, &AudioDeviceAlsa::writeSpaceAvailable));

    if (snd_pcm_prepare(play_handle) < 0)
    {
      cerr << "*** ERROR: Start playback failed" << endl;
      closeDevice();
      return false;
    }
  }

  if ((mode == MODE_RD) || (mode == MODE_RDWR))
  {
    if (snd_pcm_open (&rec_handle, dev_name.c_str(),
                      SND_PCM_STREAM_CAPTURE, 0) < 0)
    {
      cerr << "*** ERROR: Open capture audio device failed" << endl;
      return false;
    }

    if (!initParams(rec_handle))
    {
      closeDevice();
      return false;
    }

    int rec_nfds = snd_pcm_poll_descriptors_count(rec_handle);

    pollfd rec_pfds[rec_nfds];
    snd_pcm_poll_descriptors(rec_handle, rec_pfds, rec_nfds);
            
    rec_watch = new AlsaWatch(rec_pfds, rec_nfds);
    rec_watch->activity.connect(
            slot(*this, &AudioDeviceAlsa::audioReadHandler));

    if (snd_pcm_prepare(rec_handle) < 0)
    {
      cerr << "*** ERROR: Start capture failed" << endl;
      closeDevice();
      return false;
    }

    if (snd_pcm_start(rec_handle) < 0)
    {
      cerr << "*** ERROR: Start capture failed" << endl;
      closeDevice();
      return false;
    }
  }

  return true;

} /* AudioDeviceAlsa::openDevice */


void AudioDeviceAlsa::closeDevice(void)
{
  if (play_handle != 0)
  {
    snd_pcm_close(play_handle);
    play_handle = 0;
    delete play_watch;
    play_watch = 0;
  }

  if (rec_handle != 0)
  {
    snd_pcm_close(rec_handle);
    rec_handle = 0;
    delete rec_watch;
    rec_watch = 0;
  }
} /* AudioDeviceAlsa::closeDevice */



/****************************************************************************
 *
 * Private member functions
 *
 ****************************************************************************/


void AudioDeviceAlsa::audioReadHandler(FdWatch *watch, pollfd *pfd)
{
  assert(rec_handle != 0);
  assert((mode() == MODE_RD) || (mode() == MODE_RDWR));
  
  unsigned short revents;
  snd_pcm_poll_descriptors_revents(rec_handle, pfd, 1, &revents);
   
  if (!(revents & POLLIN))
  {
    return;
  }  

  int frames_avail = snd_pcm_avail_update(rec_handle);
  if ((frames_avail < 0) ||
      ((frames_avail == 0) && (snd_pcm_state(rec_handle) != SND_PCM_STATE_RUNNING)))
  {
    int err = snd_pcm_prepare(rec_handle);
    if (err < 0)
    {
      cerr << "*** ERROR: snd_pcm_prepare failed (unrecoverable error): "
           << snd_strerror(err)
           << endl;
      watch->setEnabled(false);
      return;
    }
    err = snd_pcm_start(rec_handle);
    if (err < 0)
    {
      cerr << "*** ERROR: snd_pcm_start failed (unrecoverable error): "
           << snd_strerror(err)
           << endl;
      watch->setEnabled(false);
    }
    return;
  }

  //printf("frames_avail=%d\n", frames_avail);

  if (frames_avail >= block_size)
  {
    int frames_to_read = min(frames_avail, 4096 / channels);

    frames_to_read /= block_size;
    frames_to_read *= block_size;

    int frames_read = snd_pcm_readi(rec_handle, buf, frames_to_read);
    if (frames_read < 0)
    {
      cerr << "*** ERROR: snd_pcm_readi in AudioDeviceAlsa::audioReadHandler"
           << endl;
      return;
    }
    assert(frames_read == frames_to_read);

    putBlocks(buf, frames_read);
  }
} /* AudioDeviceAlsa::audioReadHandler */


void AudioDeviceAlsa::writeSpaceAvailable(FdWatch *watch, pollfd *pfd)
{
  //printf("AudioDeviceAlsa::writeSpaceAvailable\n");
  
  assert(play_handle != 0);
  assert((mode() == MODE_WR) || (mode() == MODE_RDWR));

  unsigned short revents;
  snd_pcm_poll_descriptors_revents(play_handle, pfd, 1, &revents);
    
  if (!(revents & POLLOUT))
  {
    return;
  }

  while (1)
  {
    int space_avail = snd_pcm_avail_update(play_handle);

      // Bail out if there's an error
    if (space_avail < 0)
    {
      if (snd_pcm_prepare(play_handle) < 0)
      {
        cerr << "*** ERROR: Non-recoverable underrun in" <<
                "AudioDeviceAlsa::writeSpaceAvailable" << endl;
        watch->setEnabled(false);
        return;
      }
      continue;
    }

    int blocks_to_read = min(space_avail, 4096 / channels) / block_size;
    if (blocks_to_read == 0)
    {
      //printf("No free blocks available in sound card buffer\n");
      return;
    }
    
    int blocks_avail = getBlocks(buf, blocks_to_read);
    if (blocks_avail == 0)
    {
      //printf("No blocks available to write\n");
      watch->setEnabled(false);
      return;
    }
    
    int frames_to_write = blocks_avail * block_size;
    int frames_written = snd_pcm_writei(play_handle, buf, frames_to_write);
    //printf("frames_avail=%d  blocks_avail=%d  blocks_gotten=%d "
    //       "frames_written=%d\n", (int)frames_avail, blocks_avail,
    //       blocks_gotten, (int)frames_written);
    if (frames_written < 0)
    {
      if (snd_pcm_prepare(play_handle) < 0)
      {
        cerr << "*** ERROR: Non-recoverable underrun in" <<
                "AudioDeviceAlsa::writeSpaceAvailable" << endl;
        watch->setEnabled(false);
        return;
      }
      continue;
    }

    assert(frames_written == frames_to_write);
    
    if (frames_to_write != space_avail)
    {
      return;
    }
  }
}


bool AudioDeviceAlsa::initParams(snd_pcm_t *pcm_handle)
{
  snd_pcm_hw_params_t *hw_params;

  if (snd_pcm_hw_params_malloc (&hw_params) < 0)
  {
    cerr << "*** ERROR: Allocate hardware parameter structure failed " << endl;
    return false;
  }

  if (snd_pcm_hw_params_any (pcm_handle, hw_params) < 0)
  {
    cerr << "*** ERROR: Initialize hardware parameter structure failed" << endl;
    snd_pcm_hw_params_free (hw_params);
    return false;
  }

  if (snd_pcm_hw_params_set_access(pcm_handle, hw_params,
                                   SND_PCM_ACCESS_RW_INTERLEAVED) < 0)
  {
    cerr << "*** ERROR: Set access type failed" << endl;
    snd_pcm_hw_params_free (hw_params);
    return false;
  }

  if (snd_pcm_hw_params_set_format(pcm_handle, hw_params,
                                   SND_PCM_FORMAT_S16_LE) < 0)
  {
    cerr << "*** ERROR: Set sample format failed" << endl;
    snd_pcm_hw_params_free (hw_params);
    return false;
  }

  unsigned int real_rate = sample_rate;
  if (snd_pcm_hw_params_set_rate_near(pcm_handle, hw_params, &real_rate, 0) < 0)
  {
    cerr << "*** ERROR: Set sample rate failed" << endl;
    snd_pcm_hw_params_free (hw_params);
    return false;
  }

  if (::abs(real_rate - sample_rate) > 100)
  {
    cerr << "*** ERROR: The sample rate could not be set to "
         << sample_rate << "Hz for ALSA device \"" << dev_name << "\". "
         << "The closest rate returned by the driver was "
         << real_rate << "Hz."
         << endl;
    snd_pcm_hw_params_free(hw_params);
    return false;
  }

  if (snd_pcm_hw_params_set_channels(pcm_handle, hw_params, channels) < 0)
  {
    cerr << "*** ERROR: Set channel count failed" << endl;
    snd_pcm_hw_params_free (hw_params);
    return false;
  }

  snd_pcm_uframes_t period_size = block_size_hint;
  if (snd_pcm_hw_params_set_period_size_near(pcm_handle, hw_params,
                                             &period_size, 0) < 0)
  {
    cerr << "*** ERROR: Set period size failed" << endl;
    snd_pcm_hw_params_free (hw_params);
    return false;
  }
                                                                 
  block_size = period_size;
  block_count = block_count_hint * block_size_hint / block_size;

  /* Set number of periods. Periods used to be called fragments. */
  if (snd_pcm_hw_params_set_periods_near(pcm_handle, hw_params,
                                         (uint32_t *)&block_count, 0) < 0)
  {
     cerr << "*** ERROR: Set periods failed" << endl;
     snd_pcm_hw_params_free (hw_params);
     return false;
  }
  
  if (snd_pcm_hw_params(pcm_handle, hw_params) < 0)
  {
    cerr << "*** ERROR: Set hardware parameters failed" << endl;
    snd_pcm_hw_params_free (hw_params);
    return false;
  }

  snd_pcm_hw_params_free(hw_params);


  snd_pcm_sw_params_t *sw_params;
  
  if (snd_pcm_sw_params_malloc(&sw_params) < 0)
  {
    cerr << "*** ERROR: Allocate software parameter structure failed " << endl;
    return false;
  }

  if (snd_pcm_sw_params_current(pcm_handle, sw_params) < 0)
  {
    cerr << "*** ERROR: Initialize software parameter structure failed" << endl;
    snd_pcm_sw_params_free (sw_params);
    return false;
  }

  if (snd_pcm_sw_params_set_start_threshold(pcm_handle, sw_params,
                            (block_count - 1) * block_size) < 0)
  {
    cerr << "*** ERROR: Set start threshold failed" << endl;
    snd_pcm_sw_params_free (sw_params);
    return false;
  }

  if (snd_pcm_sw_params_set_avail_min(pcm_handle, sw_params, block_size) < 0)
  {
    cerr << "*** ERROR: Set min_avail threshold failed" << endl;
    snd_pcm_sw_params_free(sw_params);
    return false;
  }

  if (snd_pcm_sw_params(pcm_handle, sw_params) < 0)
  {
    cerr << "*** ERROR: Set software parameters failed" << endl;
    snd_pcm_sw_params_free (sw_params);
    return false;
  }
  
  snd_pcm_sw_params_free(sw_params);

  return true;
} /* AudioDeviceAlsa::initParams */



/*
 * This file has not been truncated
 */
