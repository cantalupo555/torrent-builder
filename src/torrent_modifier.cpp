#include "torrent_modifier.hpp"
#include "torrent_inspector.hpp"
#include "logger.hpp"
#include "utils.hpp"
#include "output.hpp"
#include <libtorrent/torrent_info.hpp>
#include <libtorrent/bencode.hpp>
#include <libtorrent/entry.hpp>
#include <libtorrent/info_hash.hpp>
#include <fstream>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <libtorrent/bdecode.hpp>

TorrentModifier::TorrentModifier(const ModifyConfig &config) : config_(config)
{
}

void TorrentModifier::modify()
{
    load();

    bool has_any_mod = config_.trackers.has_value() ||
                       !config_.add_trackers.empty() ||
                       !config_.remove_trackers.empty() ||
                       config_.is_private.has_value() ||
                       config_.source.has_value() ||
                       config_.comment.has_value() ||
                       config_.name.has_value() ||
                       config_.entropy;

    if (!has_any_mod)
    {
        log_message("No modifications requested", LogLevel::WARNING);
        print_info("No modifications requested - nothing to change\n");
        return;
    }

    bool info_modified = config_.is_private.has_value() ||
                         config_.source.has_value() ||
                         config_.name.has_value() ||
                         config_.entropy;

    lt::span<const char> orig_info_bytes;
    if (!info_modified)
    {
        lt::bdecode_node node;
        lt::error_code ec;
        if (lt::bdecode(raw_buffer_.data(), raw_buffer_.data() + raw_buffer_.size(), node, ec) == 0)
        {
            auto info = node.dict_find("info");
            if (info.type() == lt::bdecode_node::dict_t)
            {
                orig_info_bytes = info.data_section();
            }
        }
    }

    lt::entry root = lt::bdecode(lt::span<const char>(raw_buffer_.data(), raw_buffer_.size()));
    apply_modifications(root);

    std::vector<char> new_buffer;
    lt::bencode(std::back_inserter(new_buffer), root);

    if (!orig_info_bytes.empty())
    {
        lt::bdecode_node new_node;
        lt::error_code ec;
        if (lt::bdecode(new_buffer.data(), new_buffer.data() + new_buffer.size(), new_node, ec) == 0)
        {
            auto new_info = new_node.dict_find("info");
            if (new_info.type() == lt::bdecode_node::dict_t)
            {
                auto new_span = new_info.data_section();
                if (new_span.size() == orig_info_bytes.size())
                {
                    std::copy(orig_info_bytes.begin(), orig_info_bytes.end(),
                              new_buffer.begin() + (new_span.data() - new_buffer.data()));
                }
            }
        }
    }

    auto [new_hash_v1, new_hash_v2] = compute_hashes(new_buffer);
    print_diff(new_hash_v1, new_hash_v2);

    if (!config_.dry_run)
    {
        save(new_buffer);
    }
}

void TorrentModifier::load()
{
    if (!fs::exists(config_.input))
    {
        throw std::runtime_error("Torrent file does not exist: " + config_.input.string());
    }

    std::ifstream file(config_.input, std::ios::binary);
    if (!file)
    {
        throw std::runtime_error("Failed to open torrent file: " + config_.input.string());
    }

    raw_buffer_ = std::vector<char>((std::istreambuf_iterator<char>(file)),
                                     std::istreambuf_iterator<char>());

    if (raw_buffer_.empty())
    {
        throw std::runtime_error("Torrent file is empty: " + config_.input.string());
    }

    auto [h1, h2] = compute_hashes(raw_buffer_);
    old_hash_v1_ = h1;
    old_hash_v2_ = h2;
}

void TorrentModifier::apply_modifications(lt::entry &root)
{
    lt::entry *info = root.find_key("info");
    if (info == nullptr)
    {
        throw std::runtime_error("Invalid torrent: missing 'info' dictionary");
    }

    if (config_.trackers.has_value())
    {
        rebuild_trackers(root, *config_.trackers);
        log_message("Modify: replaced trackers (" + std::to_string(config_.trackers->size()) + " URLs)", LogLevel::INFO);
    }
    else if (!config_.add_trackers.empty() || !config_.remove_trackers.empty())
    {
        remove_trackers(root, config_.remove_trackers);
        add_trackers(root, config_.add_trackers);
        if (!config_.remove_trackers.empty())
            log_message("Modify: removed " + std::to_string(config_.remove_trackers.size()) + " tracker(s)", LogLevel::INFO);
        if (!config_.add_trackers.empty())
            log_message("Modify: added " + std::to_string(config_.add_trackers.size()) + " tracker(s)", LogLevel::INFO);
    }

    if (config_.is_private.has_value())
    {
        (*info)["private"] = lt::entry(*config_.is_private ? 1 : 0);
        log_message("Modify: set private=" + std::string(*config_.is_private ? "true" : "false"), LogLevel::INFO);
    }

    if (config_.source.has_value())
    {
        if (config_.source->empty())
        {
            lt::entry::dictionary_type &dict = info->dict();
            dict.erase("source");
            log_message("Modify: removed source field", LogLevel::INFO);
        }
        else
        {
            (*info)["source"] = lt::entry(*config_.source);
            log_message("Modify: set source=\"" + *config_.source + "\"", LogLevel::INFO);
        }
    }

    if (config_.comment.has_value())
    {
        if (config_.comment->empty())
        {
            lt::entry::dictionary_type &dict = root.dict();
            dict.erase("comment");
            log_message("Modify: removed comment field", LogLevel::INFO);
        }
        else
        {
            root["comment"] = lt::entry(*config_.comment);
            log_message("Modify: set comment=\"" + *config_.comment + "\"", LogLevel::INFO);
        }
    }

    if (config_.name.has_value())
    {
        (*info)["name"] = lt::entry(*config_.name);
        log_message("Modify: set name=\"" + *config_.name + "\"", LogLevel::INFO);
    }

    if (config_.entropy)
    {
        try
        {
            (*info)["entropy"] = lt::entry(utils::generate_entropy_hex());
        }
        catch (const std::exception &ex)
        {
            log_message("Failed to generate entropy: " + std::string(ex.what()), LogLevel::ERR);
            throw std::runtime_error("Failed to generate entropy: " + std::string(ex.what()));
        }
        log_message("Modify: added entropy field", LogLevel::INFO);
    }
}

void TorrentModifier::save(const std::vector<char> &buffer)
{
    fs::path output = config_.output.empty() ? config_.input : config_.output;

    if (output != config_.input && output.has_parent_path())
    {
        fs::create_directories(output.parent_path());
    }

    utils::atomic_write(output, buffer);

    print_info("Torrent saved to: " + output.string() + "\n");
    log_message("Torrent modified successfully: " + output.string(), LogLevel::INFO);
}

std::pair<std::string, std::string> TorrentModifier::compute_hashes(const std::vector<char> &buffer) const
{
    std::string hash_v1;
    std::string hash_v2;

    try
    {
        lt::torrent_info ti(lt::span<const char>(buffer.data(), buffer.size()), lt::from_span);
        auto hashes = ti.info_hashes();

        if (hashes.has_v1())
        {
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (auto b : hashes.v1)
            {
                ss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(b));
            }
            hash_v1 = ss.str();
        }

        if (hashes.has_v2())
        {
            auto v2_bytes = hashes.v2;
            std::stringstream ss;
            ss << std::hex << std::setfill('0');
            for (auto b : v2_bytes)
            {
                ss << std::setw(2) << static_cast<int>(static_cast<unsigned char>(b));
            }
            hash_v2 = ss.str();
        }
    }
    catch (const std::exception &e)
    {
        log_message("Could not compute info hash: " + std::string(e.what()), LogLevel::WARNING);
    }

    return {hash_v1, hash_v2};
}

void TorrentModifier::rebuild_trackers(lt::entry &root, const std::vector<std::string> &urls)
{
    if (urls.empty())
    {
        lt::entry::dictionary_type &dict = root.dict();
        dict.erase("announce");
        dict.erase("announce-list");
        return;
    }

    root["announce"] = lt::entry(urls[0]);

    lt::entry::list_type announce_list;
    for (const auto &url : urls)
    {
        lt::entry::list_type tier;
        tier.emplace_back(lt::entry(url));
        announce_list.push_back(lt::entry(std::move(tier)));
    }
    root["announce-list"] = lt::entry(std::move(announce_list));
}

void TorrentModifier::add_trackers(lt::entry &root, const std::vector<std::string> &urls)
{
    if (urls.empty())
    {
        return;
    }

    lt::entry *al = root.find_key("announce-list");
    if (al != nullptr && al->type() == lt::entry::list_t)
    {
        lt::entry::list_type &tiers = al->list();
        if (!tiers.empty())
        {
            lt::entry::list_type &last_tier = tiers.back().list();
            for (const auto &url : urls)
            {
                last_tier.emplace_back(lt::entry(url));
            }
        }
        else
        {
            lt::entry::list_type tier;
            for (const auto &url : urls)
            {
                tier.emplace_back(lt::entry(url));
            }
            tiers.emplace_back(lt::entry(std::move(tier)));
        }
    }
    else
    {
        lt::entry::list_type announce_list;
        lt::entry::list_type tier;
        lt::entry *ann = root.find_key("announce");
        if (ann != nullptr && ann->type() == lt::entry::string_t)
        {
            tier.emplace_back(lt::entry(ann->string()));
        }
        for (const auto &url : urls)
        {
            tier.emplace_back(lt::entry(url));
        }
        announce_list.push_back(lt::entry(std::move(tier)));
        root["announce-list"] = lt::entry(std::move(announce_list));
    }

    lt::entry *ann = root.find_key("announce");
    if (ann == nullptr || ann->type() != lt::entry::string_t)
    {
        root["announce"] = lt::entry(urls[0]);
    }
}

std::vector<std::string> TorrentModifier::remove_url_from_tiers(lt::entry::list_type &tiers,
                                                                  const std::vector<std::string> &urls)
{
    std::vector<std::string> remaining;

    for (auto tier_it = tiers.begin(); tier_it != tiers.end();)
    {
        if (tier_it->type() != lt::entry::list_t)
        {
            ++tier_it;
            continue;
        }

        lt::entry::list_type &tier_urls = tier_it->list();
        for (auto url_it = tier_urls.begin(); url_it != tier_urls.end();)
        {
            if (url_it->type() == lt::entry::string_t)
            {
                bool matches = std::any_of(urls.begin(), urls.end(),
                                           [&](const std::string &u)
                                           { return url_it->string() == u; });
                if (matches)
                {
                    url_it = tier_urls.erase(url_it);
                }
                else
                {
                    remaining.push_back(url_it->string());
                    ++url_it;
                }
            }
            else
            {
                ++url_it;
            }
        }

        tier_it = tier_urls.empty() ? tiers.erase(tier_it) : std::next(tier_it);
    }

    return remaining;
}

void TorrentModifier::update_announce_from_remaining(lt::entry &root, const std::vector<std::string> &remaining)
{
    if (!remaining.empty())
    {
        root["announce"] = lt::entry(remaining[0]);
    }
    else
    {
        lt::entry::dictionary_type &dict = root.dict();
        dict.erase("announce");
    }
}

void TorrentModifier::remove_trackers(lt::entry &root, const std::vector<std::string> &urls)
{
    if (urls.empty())
    {
        return;
    }

    std::vector<std::string> remaining;

    lt::entry *al = root.find_key("announce-list");
    if (al != nullptr && al->type() == lt::entry::list_t)
    {
        lt::entry::list_type &tiers = al->list();
        remaining = remove_url_from_tiers(tiers, urls);

        if (tiers.empty())
        {
            lt::entry::dictionary_type &dict = root.dict();
            dict.erase("announce-list");
        }
    }
    else
    {
        lt::entry *ann = root.find_key("announce");
        if (ann != nullptr && ann->type() == lt::entry::string_t)
        {
            bool matches = std::any_of(urls.begin(), urls.end(),
                                       [&](const std::string &u)
                                       { return ann->string() == u; });
            if (matches)
            {
                lt::entry::dictionary_type &dict = root.dict();
                dict.erase("announce");
            }
            else
            {
                remaining.push_back(ann->string());
            }
        }
    }

    update_announce_from_remaining(root, remaining);
}

void TorrentModifier::print_diff(const std::string &new_hash_v1, const std::string &new_hash_v2) const
{
    bool hash_changed = (new_hash_v1 != old_hash_v1_ || new_hash_v2 != old_hash_v2_);

    if (config_.dry_run)
    {
        log_message("Modify: dry-run mode - no changes written", LogLevel::INFO);
        print_info("Dry run - no changes written\n");
    }

    if (hash_changed)
    {
        if (!old_hash_v1_.empty() && !new_hash_v1.empty() && old_hash_v1_ != new_hash_v1)
        {
            print_info("Info hash v1 changed: " + old_hash_v1_ + " -> " + new_hash_v1 + "\n");
        }
        if (!old_hash_v2_.empty() && !new_hash_v2.empty() && old_hash_v2_ != new_hash_v2)
        {
            print_info("Info hash v2 changed: " + old_hash_v2_ + " -> " + new_hash_v2 + "\n");
        }
    }
}
