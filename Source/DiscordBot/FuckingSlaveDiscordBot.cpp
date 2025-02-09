#include "FuckingSlaveDiscordBot.hpp"

#include "GuelderResourcesManager.hpp"

namespace FSDB
{
    GE_DEFINE_LOG_CATEGORY(FSDB);

    using namespace GuelderConsoleLog;

    FuckingSlaveDiscordBot::FuckingSlaveDiscordBot(const std::string_view& token, const std::string_view& prefix, const std::string& yt_dlpPath, uint32_t intents)
        : DiscordBot(token, prefix, intents), m_Player(200000, true, false)
    {
        this->on_voice_state_update(
            [this](const dpp::voice_state_update_t& voiceState)
            {
                if(voiceState.state.user_id == this->me.id)
                {
                    if(!voiceState.state.channel_id.empty())
                    {
                        m_IsJoined = true;
                        LogInfo("Has just joined to ", voiceState.state.channel_id, " channel in ", voiceState.state.guild_id, " guild.");
                    }
                    else
                    {
                        m_IsJoined = false;
                        m_Player.Stop();
                        LogInfo("Has just disconnected from voice channel.");
                    }

                    m_JoinedCondition.notify_all();
                    m_Player.Pause(false);
                }
            }
        );

        //TODO:
        //support of commands like: "!play -speed .4" so to finish the Param system
        //add support of playlists
        //add support of streaming audio from raw url, like from google drive
        //add support of skipping a certain time
        //add gui?
        //add support of looking for url depending on the name so that message contain only the name, for instance, "!play "Linkpark: Numb""
        //make first decoding faster then next so to make play almost instantly
        //make beauty and try optimize all possible things
        //add possibility of making bass boost?
        //rename to Orchestra and find an avatar

        //help
        this->AddCommand({ "help",
            [this](const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
            {
                std::stringstream outStream;

                std::ranges::for_each(this->m_Commands, [&outStream, this](const Command& command) { outStream << m_Prefix << command.name << " - " << command.description << '\n'; });

                message.reply(outStream.str());
            },
            "Prints out all available commands and also the command's description if it exists."
            });
        //play
        this->AddCommand({ "play",
            [this, yt_dlpPath](const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
            {
                //join
                if(!m_IsJoined)
                {
                    if(!dpp::find_guild(message.msg.guild_id)->connect_member_voice(message.msg.author.id))
                        message.reply("You don't seem to be in a voice channel!");
                    //else
                        //message.reply("Joining your voice channel!");
                }

                if(!value.empty())
                {
                   m_IsStopped = false;
                    const auto& inUrl = value;

                   LogInfo("Received music url: ", inUrl);

                   const auto received = GuelderResourcesManager::ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist -f bestaudio --get-url \"", inUrl, '\"'), 1);

                   if(received.empty())
                   {
                       message.reply("Something went wrong...");
                       return;
                   }

                   const std::string rawUrl = received[0];
                   //const std::string title = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --print \"%(title)s\" \"", inUrl, '\"'))[0];

                   //const auto _urlCount = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist --print \"%(playlist_count)s\" \"", inUrl, '\"'), 1);
                   //const size_t urlCount = (!_urlCount.empty() && !_urlCount[0].empty() && _urlCount[0] != "NA" ? std::atoi(_urlCount[0].data()) : 0);

                   LogInfo("Received raw url to audio: ", rawUrl);

                   std::unique_lock joinLock{m_JoinMutex};
                   if(!m_JoinedCondition.wait_for(joinLock, std::chrono::seconds(10), [this] { return m_IsJoined == true; }))
                       return;

                   dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);
                   if(!v || !v->voiceclient || !v->voiceclient->is_ready())
                   {
                       message.reply("There was an issue joining the voice channel. Please make sure I am in a channel.");
                       return;
                   }

                   float speed = 1;
                   {
                       auto found = std::ranges::find_if(params, [](const Param& p) { return p.name == "speed"; });
                       if(found != params.end())
                           speed = StringToDouble(found->value);
                   }
                    size_t repeat = 1;
                   {
                       auto found = std::ranges::find_if(params, [](const Param& p) { return p.name == "repeat"; });
                       if(found != params.end())
                           repeat = StringToInt(found->value);
                   }


                   std::lock_guard lock{m_PlayMutex};
                    if(!m_IsStopped)
                    {
                        for(size_t i = 0; i < repeat && !m_IsStopped; ++i)
                        {
                            m_Player.AddAudioBack(rawUrl, Decoder::DEFAULT_SAMPLE_RATE * speed);
                           m_Player.PlayAudio(v);

                           if(m_Player.GetAudioCount())
                               m_Player.DeleteAudio();
                        }
                        if(m_Player.GetAudioCount())
                               m_Player.DeleteAllAudio();
                    }
                    /*for(size_t i = 1; i <= urlCount; ++i)
                    {
                        const std::string currentUrl = ResourcesManager::ExecuteCommand(Logger::Format(yt_dlpPath, " --flat-playlist -f bestaudio --get-url --playlist-items", i, " \"", inUrl, '\"'))[0];
                        PlayAudio(v, rawUrl, bufferSize, logSentPackets);
                    }*/
                 }
                 else
                 {
                    auto found = std::ranges::find_if(params, [](const Param& p) { return p.name == "speed"; });
                    if(found != params.end())
                    {
                        float speed = StringToDouble(found->value);

                        auto foundIndex = std::ranges::find_if(params, [](const Param& p) { return p.name == "index"; });

                        size_t index = (foundIndex != params.end() ? StringToInt(foundIndex->value) : 0);

                        if(index < params.size())
                            m_Player.SetAudioSampleRate(Decoder::DEFAULT_SAMPLE_RATE * speed, index);
                    }
                 }

                 LogError("exiting playing audio");
             },
             "I should join your voice channel and play audio from youtube, soundcloud and all other stuff that yt-dlp supports."
            });
        //stop
        this->AddCommand({ "stop",
            [this](const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
            {
                    dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

                    if(!v)
                    {
                        LogError("this->get_shard(0)->get_voice(message.msg.guild_id) == nullptr for some reason");
                        return;
                    }

                    m_Player.Stop();
                    m_IsStopped = true;

                    v->voiceclient->pause_audio(m_Player.GetIsPaused());
                    v->voiceclient->stop_audio();
            },
            "Stops all audio"
            });
        //pause
        this->AddCommand({ "pause",
            [this](const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
            {
                if(m_IsJoined)
                {
                    dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

                    if(!v)
                    {
                        LogError("this->get_shard(0)->get_voice(message.msg.guild_id) == nullptr for some reason");
                        return;
                    }

                    m_Player.Pause(!m_Player.GetIsPaused());
                    v->voiceclient->pause_audio(m_Player.GetIsPaused());

                    message.reply(Logger::Format("Pause is ", (m_Player.GetIsPaused() ? "on" : "off"), '.'));
                }
                else
                    message.reply("Why should I pause?");
            },
            "Pauses the audio"
            });
        //skip
        this->AddCommand({ "skip",
            [this](const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
            {
                dpp::voiceconn* v = this->get_shard(0)->get_voice(message.msg.guild_id);

                if(!v)
                {
                    LogError("this->get_shard(0)->get_voice(message.msg.guild_id) == nullptr for some reason");
                    return;
                }

                v->voiceclient->stop_audio();
                m_Player.Skip();
            },
            "Skips current track"
            });
        //leave
        this->AddCommand({ "leave",
            [this](const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
            {
                dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

                if(voice && voice->voiceclient && voice->is_ready())
                {
                    m_Player.Stop();

                    voice->voiceclient->stop_audio();
                    this->get_shard(0)->disconnect_voice(message.msg.guild_id);

                    message.reply(Logger::Format("Leaving."));
                }
                else
                    message.reply("I'm not in a voice channel");
            },
            "Disconnects from a voice channel if it is in it."
            });
        //terminate
        this->AddCommand({ "terminate",
            [this](const dpp::message_create_t& message, const std::vector<Param>& params, const std::string_view& value)
            {
                if(message.msg.author.id == 465169363230523422)//k03440k
                {
                    //disconnect
                    dpp::voiceconn* voice = this->get_shard(0)->get_voice(message.msg.guild_id);

                    if(voice && voice->voiceclient && voice->is_ready())
                    {
                        this->get_shard(0)->disconnect_voice(message.msg.guild_id);
                    }

                    message.reply("Goodbye!");

                    //std::unique_lock lock{m_JoinMutex};
                    //m_JoinedCondition.wait_for(lock, std::chrono::milliseconds(500), [this] { return m_IsJoined == false; });

                    exit(0);
                    }
                }
            });
    }

    void FuckingSlaveDiscordBot::SetEnableLogSentPackets(const bool& enable)
    {
        m_Player.SetEnableLogSentPackets(enable);
    }
    void FuckingSlaveDiscordBot::SetEnableLazyDecoding(const bool& enable)
    {
        m_Player.SetEnableLazyDecoding(enable);
    }
    void FuckingSlaveDiscordBot::SetSentPacketSize(const uint32_t& size)
    {
        m_Player.SetSentPacketSize(size);
    }

    bool FuckingSlaveDiscordBot::GetEnableLogSentPackets() const noexcept
    {
        return m_Player.GetEnableLogSentPackets();
    }
    bool FuckingSlaveDiscordBot::GetEnableLazyDecoding() const noexcept
    {
        return m_Player.GetEnableLazyDecoding();
    }
    uint32_t FuckingSlaveDiscordBot::GetSentPacketSize() const noexcept
    {
        return m_Player.GetSentPacketSize();
    }
}
