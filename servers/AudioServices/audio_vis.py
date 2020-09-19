import logging
import numpy as np

from servers.base_service_models import ByteStreamService
from Realtime_PyAudio_FFT.src.stream_analyzer import Stream_Analyzer


class AudioVisualizerService(ByteStreamService):

    QOS = 1

    @property
    def service_info(self):
        return {"service_name":"service/audio_vis",
                "topic":"audio_vis"}

    def init_service(self, fps=60, avg_energy_height=0.1525, decay_speed=0.1):
        super(AudioVisualizerService, self).init_service(fps=fps)
        self.avg_energy_height = avg_energy_height
        self.decay_speed = decay_speed
        self.audio_analyzer = Stream_Analyzer(
            device = 0,               # Manually play with this (int) if you don't see anything
            rate   = None,               # Audio samplerate, None uses the default source settings
            FFT_window_size_ms  = 60,    # Window size used for the FFT transform
            updates_per_second  = 1000,  # How often to read the audio stream for new data
            smoothing_length_ms = 50,    # Apply some temporal smoothing to reduce noisy features
            n_frequency_bins    = 64,   # The FFT features are grouped in bins
            visualize = 1,               # Visualize the FFT features with PyGame
            verbose   = 0                # Print running statistics (latency, fps, ...)
            )
        self.old_fast_bar_values = np.zeros([64])
        self.old_slow_bar_values = np.zeros([64])

    def start_service(self):
        super(AudioVisualizerService, self).start_service()

    def calc_norm_bar_heights(self, stream_analyzer, binned_fft):
        if np.min(stream_analyzer.bin_mean_values) > 0:
            frequency_bin_energies = self.avg_energy_height * \
                stream_analyzer.frequency_bin_energies / stream_analyzer.bin_mean_values

            feature_values = frequency_bin_energies

            fast_decay = np.minimum(0.99, 1 - np.maximum(0, self.decay_speed * 60 / stream_analyzer.fft_fps))
            fast_bar_values = np.maximum(self.old_fast_bar_values * fast_decay, feature_values)
            self.old_fast_bar_values = fast_bar_values
            
            slow_decay = np.minimum(0.99, 1 - np.maximum(0, self.decay_speed / 5 * 60 / stream_analyzer.fft_fps))
            slow_bar_values = np.maximum(self.old_slow_bar_values * slow_decay, feature_values)
            self.old_slow_bar_values = slow_bar_values

            return np.around(fast_bar_values, 3).tolist(), np.around(slow_bar_values, 3).tolist()
        else:
            return [[0.] * stream_analyzer.n_frequency_bins] * 2

    def step(self):
        try:
            # logging.debug('executing step')
            _, _, _, binned_fft = self.audio_analyzer.get_audio_features()
            fast_bar_values, slow_bar_values = self.calc_norm_bar_heights(self.audio_analyzer, binned_fft)
            self.publish({'fast_bar_values': fast_bar_values,
                          'slow_bar_values': slow_bar_values})
        except Exception as e:
            raise e
            logging.warning("Error in processing the step")


# End
