#include "torrent_creator.hpp"
#include <libtorrent/version.hpp>
#include <iomanip>
#include <cmath>
#include <openssl/evp.h> // Para cálculo de SHA-256 usando API EVP
#include <fstream>
#include <sstream>

TorrentCreator::TorrentCreator(const TorrentConfig& config)
    : config_(config) {}

std::string TorrentCreator::calculate_file_hash(const fs::path& file_path) const {
    std::ifstream file(file_path, std::ios::binary);
    if (!file) {
        throw std::runtime_error("Failed to open file: " + file_path.string());
    }

    // Criar contexto EVP
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) {
        throw std::runtime_error("Failed to create EVP context");
    }

    // Inicializar o contexto para SHA-256
    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to initialize SHA-256 digest");
    }

    // Ler o arquivo em blocos e atualizar o hash
    char buffer[8192];
    while (file.good()) {
        file.read(buffer, sizeof(buffer));
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(ctx);
            throw std::runtime_error("Failed to update SHA-256 digest");
        }
    }

    // Finalizar o cálculo do hash
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int length;
    if (EVP_DigestFinal_ex(ctx, hash, &length) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("Failed to finalize SHA-256 digest");
    }

    // Liberar o contexto
    EVP_MD_CTX_free(ctx);

    // Converter o hash para uma string hexadecimal
    std::ostringstream oss;
    for (unsigned int i = 0; i < length; i++) {
        oss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
    }

    return oss.str();
}

void TorrentCreator::verify_file_integrity() const {
    if (fs::is_directory(config_.path)) {
        for (const auto& entry : fs::recursive_directory_iterator(config_.path)) {
            if (entry.is_regular_file()) {
                std::string hash = calculate_file_hash(entry.path());
                std::cout << "Verified: " << entry.path() << " (SHA-256: " << hash << ")\n";
            }
        }
    } else if (fs::is_regular_file(config_.path)) {
        std::string hash = calculate_file_hash(config_.path);
        std::cout << "Verified: " << config_.path << " (SHA-256: " << hash << ")\n";
    } else {
        throw std::runtime_error("Invalid path: " + config_.path.string());
    }
}

int TorrentCreator::auto_piece_size(int64_t total_size) {
    if (total_size < 64 * 1024 * 1024) return 16 * 1024;
    if (total_size < 512 * 1024 * 1024) return 32 * 1024;
    return 64 * 1024;
}

int TorrentCreator::get_torrent_flags() const {
    lt::create_flags_t flags = {};
    
    switch(config_.version) {
        case TorrentVersion::V1:
            flags |= lt::create_torrent::v1_only;
            break;
        case TorrentVersion::V2:
            flags |= lt::create_torrent::v2_only;
            break;
        case TorrentVersion::HYBRID:
            // No flags for hybrid
            break;
    }
    
    return static_cast<int>(flags);
}

void TorrentCreator::add_files_to_storage() {
    if (fs::is_directory(config_.path)) {
        lt::add_files(fs_, config_.path.string(), [](std::string const&) { return true; });
    } else {
        fs_.add_file(config_.path.filename().string(), fs::file_size(config_.path));
    }
}

void TorrentCreator::create_torrent() {
    try {
        // Verifica a integridade dos arquivos antes de prosseguir
        std::cout << "Verifying file integrity...\n";
        verify_file_integrity();
        std::cout << "All files verified successfully.\n";

        add_files_to_storage();
        
        int piece_size = auto_piece_size(fs_.total_size());
        int flags = get_torrent_flags();
        
        lt::create_torrent t(fs_, piece_size, lt::create_flags_t(flags));
        
        // Set private flag if needed
        if (config_.is_private) {
            t.set_priv(true);
        }
        
        // Add trackers
        for (const auto& tracker : config_.trackers) {
            t.add_tracker(tracker);
        }
        
        // Add web seeds
        for (const auto& web_seed : config_.web_seeds) {
            t.add_url_seed(web_seed);
        }
        
        // Set comment if provided
        if (config_.comment) {
            t.set_comment(config_.comment->c_str());
        }
        
        // Set piece hashes
        lt::set_piece_hashes(t, config_.path.parent_path().string());
        
        // Generate and save torrent file
        std::ofstream out(config_.output, std::ios_base::binary);
        lt::entry e = t.generate();
        lt::bencode(std::ostream_iterator<char>(out), e);
        
        print_torrent_summary(fs_.total_size(), piece_size, t.num_pieces());
    }
    catch (const std::exception& e) {
        std::cerr << "Error creating torrent: " << e.what() << std::endl;
        throw;
    }
}

void TorrentCreator::print_torrent_summary(int64_t total_size, int piece_size, int num_pieces) const {
    std::cout << "\n=== TORRENT CREATED SUCCESSFULLY ===\n";
    std::cout << "File: " << config_.output << "\n";
    
    auto format_size = [](int64_t bytes) -> std::string {
        const char* units[] = {"B", "KB", "MB", "GB", "TB"};
        int unit = 0;
        double size = bytes;
        
        while (size >= 1024 && unit < 4) {
            size /= 1024;
            unit++;
        }
        
        std::ostringstream oss;
        oss << std::fixed << std::setprecision(2) << size << " " << units[unit];
        return oss.str();
    };
    
    std::cout << "Total size: " << format_size(total_size) << "\n";
    std::cout << "Pieces: " << num_pieces << " of " << piece_size/1024 << "KB\n";
    std::cout << "Trackers: " << config_.trackers.size() << "\n";
    std::cout << "Web seeds: " << config_.web_seeds.size() << "\n";
    std::cout << "Private: " << (config_.is_private ? "Yes" : "No") << "\n";
}
