#pragma once

#include <atomic>
#include <mutex>
#include <future>
#include <vector>
#include <string_view>

#include <dpp/dpp.h>

#include <GuelderConsoleLog.hpp>

#include "../FFmpeg/Decoder.hpp"
#include "../Utils.hpp"

namespace FSDB
{
	class Player
	{
	public:
		Player(const uint32_t& sentPacketsSize = 200000, const bool& enableLazyDecoding = true, const bool& enableLogSentPackets = false);

		//blocks current thread
		void PlayAudio(const dpp::voiceconn* voice, const size_t& index = 0);

		void AddAudio(const std::string_view& url, const uint32_t& sampleRate = 48000, const size_t& pos = 0);
		void AddAudioBack(const std::string_view& url, const uint32_t& sampleRate = 48000);

		void DeleteAudio(const size_t& index = 0);
		void DeleteAllAudio();

		void Stop();
		void Pause(const bool& pause);

		void Reserve(const size_t& capacity);

	public:
		void SetAudioSampleRate(const uint32_t& sampleRate, const size_t& index);

		void SetEnableLogSentPackets(const bool& enable);
		void SetEnableLazyDecoding(const bool& enable);
		void SetSentPacketSize(const uint32_t& size);

		bool GetIsPaused() const noexcept;
		bool GetIsDecoding() const noexcept;

		bool GetEnableLogSentPackets() const noexcept;
		bool GetEnableLazyDecoding() const noexcept;
		uint32_t GetSentPacketSize() const noexcept;
		
		size_t GetAudioCount() const noexcept;

	private:
		std::vector<Decoder> m_Decoders;

		uint32_t m_SentPacketSize;
		bool m_EnableLazyDecoding : 1;
		bool m_EnableLogSentPackets : 1;

		std::atomic_bool m_IsDecoding;

		std::atomic_bool m_IsPaused;
		std::condition_variable m_PauseCondition;
		std::mutex m_PauseMutex;
	};
}