// SPDX-License-Identifier: GPL-3.0-or-later
#include "o1602/render/sprite_atlas.hpp"

#include <algorithm>
#include <array>
#include <thread>
#include <cstdio>
#include <mutex>
#include <chrono>
#include <cerrno>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdexcept>
#ifdef O1602_EXPERIMENTAL_VITA
#include <psp2/io/fcntl.h>
#endif
#include "o1602/core/compress.hpp"
#include "o1602/core/hash.hpp"
#include "o1602/core/file.hpp"
#include "o1602/core/byte_reader.hpp"
#include "o1602/core/byte_writer.hpp"
#include "o1602/core/profile.hpp"
#include "o1602/core/workers.hpp"
#include "o1602/core/block_reader.hpp"

namespace o1602::render {
namespace {std::atomic<std::uint64_t> resident_backings{0},sprite_disk_reads{0};
constexpr std::uint64_t resident_limit=16*1024*1024;
std::atomic<bool> test_no_replace{false};}

struct SpriteAtlas::Backing {
    struct Record { long offset=0; std::size_t bytes=0; std::uint64_t hash=0; };
    std::FILE* file=nullptr;
#ifdef O1602_EXPERIMENTAL_VITA
    std::atomic<int> read_fd{-1};
    int read_handle(){
        int fd=read_fd.load(std::memory_order_acquire);if(fd>=0)return fd;
        // No SDK operation under the shared backing mutex. Concurrent first
        // readers publish one descriptor; the loser closes only its own open.
        const int opened=sceIoOpen(path.string().c_str(),SCE_O_RDONLY,0);if(opened<0)return opened;
        int empty=-1;
        if(read_fd.compare_exchange_strong(empty,opened,std::memory_order_acq_rel))return opened;
        sceIoClose(opened);return empty;
    }
    static bool read_at(int fd,const Record& r,std::span<std::uint8_t> bytes){
        if(fd<0)return false;
        for(std::size_t done=0;done<bytes.size();){const int n=sceIoPread(fd,bytes.data()+done,static_cast<unsigned>(bytes.size()-done),static_cast<SceOff>(r.offset)+static_cast<SceOff>(done));if(n<=0)return false;core::profile::increment(core::profile::SpriteReadCalls);core::profile::increment(core::profile::SpriteReadBytes,static_cast<unsigned>(n));done+=static_cast<unsigned>(n);}
        return true;
    }
#endif
    std::filesystem::path path;
    std::vector<Record> records;
    std::mutex mutex;
    std::uint64_t bytes=0;
    std::unique_ptr<core::BlockReader> blocks;
    bool direct_read(std::uint64_t offset,std::span<std::uint8_t> output){
        core::profile::Scope io(core::profile::FileIO);
#ifdef O1602_EXPERIMENTAL_VITA
        const int fd=read_handle();
        core::profile::Scope reading(core::profile::SpriteBackingIO);
        return read_at(fd,Record{static_cast<long>(offset),output.size(),0},output);
#else
        std::unique_lock<std::mutex> lock;
        {core::profile::Scope waiting(core::profile::SpriteFileLock);lock=std::unique_lock(mutex);}
        core::profile::Scope reading(core::profile::SpriteBackingIO);
        const bool ok=std::fseek(file,static_cast<long>(offset),SEEK_SET)==0&&std::fread(output.data(),1,output.size(),file)==output.size();
        core::profile::increment(core::profile::SpriteReadCalls);core::profile::increment(core::profile::SpriteReadBytes,output.size());
        return ok;
#endif
    }
    bool read_record(const Record& record,std::span<std::uint8_t> output){
        core::BlockReader* reader;
        {std::unique_lock lock(mutex,std::try_to_lock);
            if(!lock.owns_lock())return direct_read(static_cast<std::uint64_t>(record.offset),output);
            // Temporary build files are still mutable (and may be truncated by
            // the existing recovery path). Read-ahead is for published caches only.
            if(!blocks){bool enabled=!temporary;
#ifdef O1602_EXPERIMENTAL_VITA
                std::error_code ec;enabled=enabled&&!std::filesystem::exists("ux0:data/open1602/sprite-block-cache-off.flag",ec);
#endif
                blocks=std::make_unique<core::BlockReader>(bytes,[this](auto offset,auto buffer){return direct_read(offset,buffer);},enabled);
            }
            reader=blocks.get();
        }
        return reader->read(static_cast<std::uint64_t>(record.offset),output);
    }
    std::unique_ptr<core::ZlibDeflater> compressor;
    bool temporary=true,corrupt=false;
    std::array<char,65536> io_buffer{};
    std::vector<std::uint8_t> resident;
    Backing()=default;
    explicit Backing(const std::filesystem::path& dir) {
        std::error_code ec;std::filesystem::create_directories(dir,ec);
        if(ec)throw std::runtime_error("cannot create private atlas cache: "+ec.message());
        static std::atomic<unsigned> serial{0};
        path=dir/("atlas-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+"-"+std::to_string(serial++)+".cache");
        file=std::fopen(path.string().c_str(),"w+b");
        if(!file)throw std::runtime_error("cannot open private atlas cache");
        std::setvbuf(file,io_buffer.data(),_IOFBF,io_buffer.size());
    }
    ~Backing(){
#ifdef O1602_EXPERIMENTAL_VITA
        if(read_fd>=0)sceIoClose(read_fd);
#endif
        if(file)std::fclose(file);core::profile::gauge(core::profile::SpriteRamBytes,resident_backings.fetch_sub(resident.size())-resident.size());if(temporary&&!path.empty())std::remove(path.string().c_str());}
    void append(std::uint32_t sprite,std::span<const std::uint8_t> pixels,std::span<const std::uint8_t> packed={}) {
        if(packed.empty()){
            if(!compressor)compressor=std::make_unique<core::ZlibDeflater>(1);
            auto encoded=[&]{core::profile::Scope compress(core::profile::AtlasCompress);return compressor->encode(pixels);}();
            if(!encoded)throw std::runtime_error(encoded.error().message);packed=*encoded;
        }
        core::profile::Scope timer(core::profile::FileIO);
        const long offset=static_cast<long>(bytes);
        if(offset<0||std::fwrite(packed.data(),1,packed.size(),file)!=packed.size())
            throw std::runtime_error("cannot write private atlas cache");
        if(bytes+packed.size()>112ULL*1024*1024)throw std::runtime_error("private sprite cache exceeds 112 MiB bound");
        records[sprite]={offset,packed.size(),core::xxh64(packed)};bytes+=packed.size();
    }
};
std::uint64_t SpriteAtlas::read_offset(std::uint32_t sprite) const noexcept {
    return m_backing&&sprite<m_backing->records.size()?static_cast<std::uint64_t>(m_backing->records[sprite].offset):sprite;
}
core::Result<RgbaImage> SpriteAtlas::read_sprite(std::uint32_t sprite) const {
    core::profile::Scope read_timer(core::profile::SpriteReadMain);
    if(sprite>=m_rects.size())return core::make_error(core::ErrorCode::OutOfRange,"atlas sprite index");
    const auto& rect=m_rects[sprite];
    if(rect.empty())return RgbaImage{};
    if(!m_backing){
        const auto& page=m_pages[rect.page];
        if(page.pixels.empty())return core::make_error(core::ErrorCode::IoError,"atlas pixels released");
        RgbaImage result{rect.width,rect.height,std::vector<std::uint8_t>(std::size_t{rect.width}*rect.height*4)};
        for(unsigned y=0;y<rect.height;++y)std::copy_n(page.pixels.data()+((std::size_t{rect.y}+y)*page.width+rect.x)*4,rect.width*4,result.pixels.data()+std::size_t{y}*rect.width*4);
        return result;
    }
    std::unique_lock lock(m_backing->mutex,std::try_to_lock);
    const auto& r=m_backing->records[sprite];std::vector<std::uint8_t> storage;
    std::span<const std::uint8_t> packed;
    bool read_ok=false;
    if(lock.owns_lock()&&!m_backing->resident.empty()){
        packed=std::span(m_backing->resident).subspan(static_cast<std::size_t>(r.offset),r.bytes);read_ok=true;
    }else{
        storage.resize(r.bytes);packed=storage;
        if(lock.owns_lock())lock.unlock();read_ok=m_backing->read_record(r,storage);
        core::profile::gauge(core::profile::SpriteDiskReads,++sprite_disk_reads);
    }
    if(lock.owns_lock())lock.unlock(); // Only the shared seek/read needs serialization, not checksum/inflate.
    if(!read_ok)
        return core::make_error(core::ErrorCode::IoError,"cannot read private sprite cache");
    if(core::xxh64(packed)!=r.hash){
        // Invalidate the index for the next preparation; never publish damaged
        // pixels to the GPU, and let the existing safe zoom rollback handle this.
        std::remove((m_backing->path.string()+".index").c_str());
        return core::make_error(core::ErrorCode::InvalidFormat,"private sprite cache checksum; rebuild required");
    }
    auto pixels=[&]{core::profile::Scope inflate(core::profile::SpriteInflate);return core::zlib_inflate(packed,std::size_t{rect.width}*rect.height*4);}();
    if(!pixels)return pixels.error();
    if(pixels->size()!=std::size_t{rect.width}*rect.height*4)return core::make_error(core::ErrorCode::InvalidFormat,"sprite cache pixel length");
    return RgbaImage{rect.width,rect.height,std::move(*pixels)};
}
std::function<core::Result<RgbaImage>()> SpriteAtlas::reader(std::uint32_t sprite) const {
    if(!m_backing||sprite>=m_rects.size()||m_rects[sprite].empty())return {};
    const auto rect=m_rects[sprite];const auto record=m_backing->records[sprite];
    // Bound the worker's compressed scratch as well as its RGBA reservation.
    if(std::size_t(rect.width)*rect.height*4>2*1024*1024||record.bytes>2304*1024)return {};
    std::unique_lock lock(m_backing->mutex,std::try_to_lock);
    if(!lock.owns_lock())return {}; // speculative work may always be skipped
    const auto resident=std::span<const std::uint8_t>(m_backing->resident);
    return [owner=m_backing,resident,record,rect]()->core::Result<RgbaImage>{
        core::profile::Scope read_timer(core::profile::SpriteReadWorker);
        std::vector<std::uint8_t> storage;std::span<const std::uint8_t> packed;
        if(!resident.empty())packed=resident.subspan(std::size_t(record.offset),record.bytes);
        else {
            storage.resize(record.bytes);
            if(!owner->read_record(record,storage))return core::make_error(core::ErrorCode::IoError,"sprite prefetch read failed");
            packed=storage;core::profile::gauge(core::profile::SpriteDiskReads,++sprite_disk_reads);
        }
        if(core::xxh64(packed)!=record.hash)return core::make_error(core::ErrorCode::InvalidFormat,"sprite prefetch checksum mismatch");
        auto pixels=core::zlib_inflate(packed,std::size_t(rect.width)*rect.height*4);
        if(!pixels)return pixels.error();
        if(pixels->size()!=std::size_t(rect.width)*rect.height*4)
            return core::make_error(core::ErrorCode::InvalidFormat,"sprite prefetch pixel length");
        return RgbaImage{rect.width,rect.height,std::move(*pixels)};
    };
}
core::Result<RgbaImage> SpriteAtlas::read_page(std::size_t index) const {
    if(index>=m_pages.size())return core::make_error(core::ErrorCode::OutOfRange,"atlas page index");
    const auto& page=m_pages[index];
    if(!m_backing)return page;
    RgbaImage result{page.width,page.height,std::vector<std::uint8_t>(std::size_t{page.width}*page.height*4,0)};
    for(std::uint32_t sprite=0;sprite<m_rects.size();++sprite){const auto& r=m_rects[sprite];
        if(r.page!=index||r.empty())continue;
        auto image=read_sprite(sprite);if(!image)return image.error();
        for(unsigned y=0;y<r.height;++y)std::copy_n(image->pixels.data()+std::size_t{y}*r.width*4,r.width*4,result.pixels.data()+((std::size_t{r.y}+y)*page.width+r.x)*4);
    }
    return result;
}
std::uint64_t SpriteAtlas::backing_bytes() const noexcept {return m_backing?m_backing->bytes:0;}
bool SpriteAtlas::warm_read_cache() {
    if(!m_backing)return false;
    auto& b=*m_backing;std::lock_guard lock(b.mutex);
    if(!b.resident.empty())return true;
    auto used=resident_backings.load();
    do {if(b.bytes>resident_limit-used)return false;}
    while(!resident_backings.compare_exchange_weak(used,used+b.bytes));
    try {
        std::vector<std::uint8_t> bytes(b.bytes);
        const bool ok=[&]{core::profile::Scope io(core::profile::FileIO);
            if(std::fseek(b.file,0,SEEK_SET)!=0)return false;
            for(std::size_t at=0;at<bytes.size();){const auto n=std::min<std::size_t>(65536,bytes.size()-at);
                if(std::fread(bytes.data()+at,1,n,b.file)!=n)return false;at+=n;
                if(at%(1024*1024)==0)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
            return true;}();
        if(!ok){resident_backings.fetch_sub(b.bytes);return false;}
        for(const auto& r:b.records)if(r.bytes&&core::xxh64(std::span(bytes).subspan(static_cast<std::size_t>(r.offset),r.bytes))!=r.hash){
            std::remove((b.path.string()+".index").c_str());b.corrupt=true;resident_backings.fetch_sub(b.bytes);return false;
        }
        b.resident=std::move(bytes);core::profile::gauge(core::profile::SpriteRamBytes,resident_backings.load());return true;
    }catch(...){resident_backings.fetch_sub(b.bytes);return false;}
}

namespace {
constexpr std::uint64_t cache_magic=0x354548434143414fULL; // OACACHE5, little endian
constexpr std::uint64_t cache_limit=112ULL*1024*1024, index_limit=20ULL*1024*1024;
std::atomic<std::uint64_t> next_identity{1};
}
std::span<const std::uint8_t> SpriteAtlas::alpha(std::uint32_t sprite) const {
    if(sprite>=m_alpha_offsets.size()||m_alpha.empty())return {};
    const auto& r=rect(sprite);const auto size=(std::size_t{r.width}*r.height+7)/8;
    return std::span(m_alpha).subspan(m_alpha_offsets[sprite],size);
}
void SpriteAtlas::drop_alpha() noexcept {std::vector<std::uint8_t>().swap(m_alpha);std::vector<std::uint32_t>().swap(m_alpha_offsets);}
void test_prepared_cache_no_replace(bool enabled){test_no_replace=enabled;}
std::optional<SpriteAtlas> SpriteAtlas::load_prepared(const std::filesystem::path& path,std::uint64_t key,std::uint32_t page_size,GraphicsProgress* progress) {
    const auto miss=[&](const char* why)->std::optional<SpriteAtlas>{
        std::fprintf(stderr,"PREPARED_CACHE_MISS file=%s reason=%s\n",path.filename().string().c_str(),why);return {};
    };
    std::error_code ec;const auto index=path.string()+".index";
    const auto size=std::filesystem::file_size(index,ec);if(ec||size<48||size>index_limit)return miss("index_missing_or_size");
    const auto bytes=std::filesystem::file_size(path,ec);if(ec||bytes>cache_limit)return miss("payload_missing_or_size");
    auto data=core::read_file(index);if(!data)return miss("index_read");
    core::ByteReader checksum{std::span(*data).last(8)};
    if(core::xxh64(std::span(*data).first(data->size()-8))!=checksum.u64().value_or(0))return miss("index_checksum");
    core::ByteReader in{std::span(*data).first(data->size()-8)};
    const auto magic=in.u64(),stored_key=in.u64();const auto stored_page=in.u32();
    if(magic!=cache_magic)return miss("cache_format");
    if(stored_key!=key)return miss("resource_key");
    if(stored_page!=page_size)return miss("page_size");
    const auto count=in.u32().value_or(0),pages=in.u32().value_or(0),bases=in.u32().value_or(0),alpha_size=in.u32().value_or(0);
    if(!count||count>100000||!pages||pages>128||!bases||bases>128||alpha_size>16*1024*1024||
        in.remaining()!=std::uint64_t{count}*30+std::uint64_t{bases}*4+alpha_size)return miss("index_layout");
    SpriteAtlas atlas;atlas.m_page_size=page_size;atlas.m_identity=next_identity++;
    atlas.m_rects.resize(count);atlas.m_alpha_offsets.resize(count);atlas.m_pages.resize(pages);
    atlas.m_backing=std::make_shared<Backing>();auto& b=*atlas.m_backing;
    b.path=path;b.temporary=false;b.bytes=bytes;b.records.resize(count);
    for(auto& page:atlas.m_pages){page.width=page.height=page_size;}
    for(std::uint32_t i=0;i<count;++i){
        if(progress && i%64==0)progress->set(GraphicsPhase::Loading,i,count);
        auto& r=atlas.m_rects[i];r.page=in.u16().value();r.x=in.u16().value();r.y=in.u16().value();r.width=in.u16().value();r.height=in.u16().value();
        auto& record=b.records[i];record.offset=static_cast<long>(in.u32().value());record.bytes=in.u32().value();record.hash=in.u64().value();atlas.m_alpha_offsets[i]=in.u32().value();
        if(r.page>=pages||std::uint32_t{r.x}+r.width>page_size||std::uint32_t{r.y}+r.height>page_size||
            std::uint64_t{static_cast<unsigned long>(record.offset)}+record.bytes>bytes||
            std::uint64_t{atlas.m_alpha_offsets[i]}+(std::uint64_t{r.width}*r.height+7)/8>alpha_size||
            (!r.empty()&&(!record.bytes||record.bytes>std::uint64_t{r.width}*r.height*4+4096)))return miss("sprite_record");
        atlas.m_max_width=std::max(atlas.m_max_width,int(r.width));atlas.m_max_height=std::max(atlas.m_max_height,int(r.height));
    }
    for(std::uint32_t i=0;i<bases;++i){const auto base=in.u32().value();if(base>count)return miss("sheet_base");atlas.m_sheet_bases.push_back(base);}
    const auto at=in.offset();atlas.m_alpha.assign(data->begin()+static_cast<std::ptrdiff_t>(at),data->end()-8);
    b.file=std::fopen(path.string().c_str(),"rb");if(!b.file)return miss("payload_open");
    std::setvbuf(b.file,nullptr,_IONBF,0);
#ifdef O1602_EXPERIMENTAL_VITA
    atlas.warm_read_cache();if(b.corrupt)return miss("payload_checksum");
#endif
    if(progress)progress->set(GraphicsPhase::Loading,count,count);
    atlas.m_cache_hit=true;return atlas;
}
void SpriteAtlas::store_prepared(const std::filesystem::path& path,std::uint64_t key) {
    if(!m_backing||m_backing->bytes>cache_limit||m_alpha.size()>16*1024*1024)return;
    core::ByteWriter out;out.reserve(36+m_rects.size()*30+m_sheet_bases.size()*4+m_alpha.size()+8);
    out.u64(cache_magic);out.u64(key);out.u32(m_page_size);out.u32(static_cast<std::uint32_t>(m_rects.size()));out.u32(static_cast<std::uint32_t>(m_pages.size()));
    out.u32(static_cast<std::uint32_t>(m_sheet_bases.size()));out.u32(static_cast<std::uint32_t>(m_alpha.size()));
    for(std::size_t i=0;i<m_rects.size();++i){const auto& r=m_rects[i];const auto& rec=m_backing->records[i];
        out.u16(r.page);out.u16(r.x);out.u16(r.y);out.u16(r.width);out.u16(r.height);
        out.u32(static_cast<std::uint32_t>(rec.offset));out.u32(static_cast<std::uint32_t>(rec.bytes));out.u64(rec.hash);out.u32(m_alpha_offsets[i]);}
    for(auto base:m_sheet_bases)out.u32(base);
    out.bytes(m_alpha);out.u64(core::xxh64(out.data()));
    if(out.size()>index_limit)return;
    // Index is published last. A power loss at either rename produces a miss or
    // a checksum failure, never an unchecked partial atlas. Six fixed mode/zoom slots
    // replace old editions/keys rather than growing without limit.
    bool durable=std::fflush(m_backing->file)==0;
#if defined(_WIN32)
    durable=(_commit(_fileno(m_backing->file))==0)&&durable;
#else
    durable=(::fsync(fileno(m_backing->file))==0)&&durable;
#endif
    durable=(std::fclose(m_backing->file)==0)&&durable;m_backing->file=nullptr;
    if(!durable)throw std::runtime_error("cannot flush prepared sprite cache");
    std::error_code exists_error;const bool destination_exists=std::filesystem::exists(path,exists_error);
    int rc=0;
    if(test_no_replace&&destination_exists){errno=EEXIST;rc=-1;}
    else rc=std::rename(m_backing->path.string().c_str(),path.string().c_str());
    const int initial_errno=rc?errno:0;bool replaced=false;
    // sceIoRename/FAT can refuse an existing destination. This is a regenerable
    // stale cache slot, not a save: invalidate its index before replacing it.
    if(rc&&destination_exists){
        std::remove((path.string()+".index").c_str());
        if(std::remove(path.string().c_str())==0){
            replaced=true;rc=std::rename(m_backing->path.string().c_str(),path.string().c_str());
        }
    }
    const int publish_errno=rc?errno:0;
    if(!rc){m_backing->path=path;m_backing->temporary=false;}
    m_backing->file=std::fopen(m_backing->path.string().c_str(),"rb");
    if(!m_backing->file)throw std::runtime_error("cannot reopen prepared sprite cache");
    std::setvbuf(m_backing->file,nullptr,_IONBF,0);
    bool index_written=false;
    if(!m_backing->temporary){
        auto status=core::write_file_atomic(path.string()+".index",out.data());
        if(!status){
            // Same no-replace rule may apply to an older index. The payload is
            // complete; publishing the checksummed new index remains last.
            if(std::remove((path.string()+".index").c_str())==0)
                status=core::write_file_atomic(path.string()+".index",out.data());
        }
        index_written=bool(status);
        if(!status)std::fprintf(stderr,"prepared cache index: %s\n",status.error().message.c_str());
    }
    std::fprintf(stderr,"PREPARED_CACHE_STORE file=%s payload=%d index=%d replace_fallback=%d initial_errno=%d errno=%d bytes=%llu\n",
        path.filename().string().c_str(),!m_backing->temporary,index_written,replaced,initial_errno,publish_errno,
        static_cast<unsigned long long>(m_backing->bytes));

}

using core::ErrorCode;
using core::make_error;

core::Result<SpriteAtlas> SpriteAtlas::build(const assets::SpriteSheet& sheet, const assets::Palette& palette,
                                             std::uint32_t page_size) {
    const assets::SpriteSheet* sheets[] = {&sheet};
    return build(sheets, palette, page_size);
}

core::Result<SpriteAtlas> SpriteAtlas::build(std::span<const assets::SpriteSheet* const> sheets,
                                             const assets::Palette& palette, std::uint32_t page_size) {
    std::vector<SheetSource> sources;
    sources.reserve(sheets.size());
    for (const assets::SpriteSheet* sheet : sheets) {
        sources.emplace_back(*sheet, palette);
    }
    std::vector<const SpriteSource*> pointers;
    for (const SheetSource& s : sources) {
        pointers.push_back(&s);
    }
    return build(std::span<const SpriteSource* const>(pointers), page_size);
}

core::Result<SpriteAtlas> SpriteAtlas::build(std::span<const SpriteSource* const> sources, std::uint32_t page_size,
                                             const std::atomic<bool>* cancel, const std::filesystem::path& backing_directory,
                                             std::string_view cache_slot,std::uint64_t cache_key,GraphicsProgress* progress) {
    core::profile::Scope packing(core::profile::AtlasPack);
    constexpr std::uint32_t kPad = 1;
    if (page_size == 0 || page_size > 0xFFFF) {
        return make_error(ErrorCode::OutOfRange, "invalid atlas page size");
    }
    std::filesystem::path prepared;
    if(!backing_directory.empty()&&!cache_slot.empty()&&cache_slot.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789-")==std::string_view::npos){
        prepared=backing_directory/(std::string(cache_slot)+".prepared");
        if(auto cached=load_prepared(prepared,cache_key,page_size,progress))return std::move(*cached);
    }
    SpriteAtlas atlas;atlas.m_identity=next_identity++;
    if(!backing_directory.empty())atlas.m_backing=std::make_shared<Backing>(backing_directory);
    atlas.m_page_size = page_size;

    struct Entry {
        std::uint32_t index;  ///< global sprite index
        const SpriteSource* source;
        std::uint32_t local;
        std::uint32_t width;
        std::uint32_t height;
    };
    std::vector<Entry> entries;
    std::uint32_t total = 0;
    for (const SpriteSource* source : sources) {
        atlas.m_sheet_bases.push_back(total);
        const std::uint32_t n = source->count();
        for (std::uint32_t i = 0; i < n; ++i) {
            Entry e{total + i, source, i, 0, 0};
            source->size(i, e.width, e.height);
            entries.push_back(e);
        }
        total += n;
    }
    atlas.m_rects.resize(total);
    if(atlas.m_backing){atlas.m_backing->records.resize(total);atlas.m_alpha_offsets.resize(total);}

    // Tallest first gives dense shelves; ties by width and index keep the order deterministic.
    std::stable_sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.height != b.height ? a.height > b.height : a.width > b.width;
    });

    std::uint32_t shelf_x = 0;
    std::uint32_t shelf_y = 0;
    std::uint32_t shelf_h = 0;
    const auto new_page = [&] {
        RgbaImage page;
        page.width = page_size;
        page.height = page_size;
        if(!atlas.m_backing)page.pixels.assign(std::size_t{page_size} * page_size * 4, 0);
        atlas.m_pages.push_back(std::move(page));
        shelf_x = shelf_y = shelf_h = 0;
    };

    // The sprites are decoded in parallel, a batch at a time (at most 1024 sprites or 64 MB), and
    // placed one after the other in the order above: the atlas is the same on every machine
    // (FB-25). Sources decode from const data; TintedUnitSource guards its cache.
#ifdef O1602_EXPERIMENTAL_VITA
    constexpr std::size_t kBatchSprites = 64;
    constexpr std::size_t kBatchBytes = std::size_t{1} << 20;
    const unsigned workers = core::worker_parallelism();
#else
    constexpr std::size_t kBatchSprites = 1024;
    constexpr std::size_t kBatchBytes = std::size_t{64} << 20;
    const unsigned workers = core::worker_parallelism(std::clamp(std::thread::hardware_concurrency(), 1U, 8U));
#endif
    core::BatchWorkers pool(workers,"graphics_decode");
    std::fprintf(stderr,"ATLAS_WORKERS count=%u decoded_batch_limit=%u helper_stacks_bounded=1\n",pool.size(),unsigned(kBatchBytes));
    std::vector<std::unique_ptr<core::ZlibDeflater>> compressors(pool.size());
    std::vector<std::vector<std::uint8_t>> compressed;
    // Large sprites dominate the first batches. Count their actual pixel work,
    // rather than leaving the display near 12% until hundreds of tiny sprites.
    std::uint64_t total_pixels=0;
    for(const auto& e:entries)total_pixels+=std::uint64_t(e.width)*e.height;
    std::atomic<std::uint64_t> worked_pixels{0};
    std::vector<std::vector<std::uint8_t>> decoded;
    std::vector<std::uint8_t> valid;
    const auto fits = [&](const Entry& e) { return e.width + kPad <= page_size && e.height + kPad <= page_size; };
    const auto decode_batch = [&](std::size_t first, std::size_t count) {
        if(progress)progress->set(GraphicsPhase::Building,worked_pixels.load(),2*total_pixels);
        core::profile::Scope decoding(core::profile::AtlasDecode);
#ifdef O1602_EXPERIMENTAL_VITA
        decoded.clear();
#endif
        decoded.resize(std::max(decoded.size(), count));
        valid.assign(count, 0);
        compressed.clear();if(atlas.m_backing&&pool.size()>1)compressed.resize(count);
        pool.run(count,[&](std::size_t j,unsigned worker) {
                const Entry& e = entries[first + j];
                if (e.width == 0 || e.height == 0 || !fits(e)) {
                    return;
                }
                valid[j] =
                    e.source->pixels(e.local, decoded[j]) && decoded[j].size() >= std::size_t{e.width} * e.height * 4
                        ? 1
                        : 0;
                if(valid[j]&&atlas.m_backing&&pool.size()>1){
                    if(!compressors[worker])compressors[worker]=std::make_unique<core::ZlibDeflater>(1);
                    core::profile::Scope compress(core::profile::AtlasCompress);
                    auto packed=compressors[worker]->encode(std::span(decoded[j]).first(std::size_t(e.width)*e.height*4));
                    if(!packed)throw std::runtime_error(packed.error().message);
                    compressed[j].assign(packed->begin(),packed->end());
                }
                const auto done=worked_pixels.fetch_add(std::uint64_t(e.width)*e.height)+std::uint64_t(e.width)*e.height;
                if(progress)progress->set(GraphicsPhase::Building,done,2*total_pixels);
        });
    };

    std::size_t batch_first = 0;
    std::size_t batch_end = 0;
    for (std::size_t k = 0; k < entries.size(); ++k) {
        const Entry& e = entries[k];
        if (cancel != nullptr && cancel->load(std::memory_order_relaxed)) {
            return make_error(ErrorCode::Unsupported, "cancelled");
        }
        if (e.width == 0 || e.height == 0) {
            continue;
        }
        if (!fits(e)) {
            return make_error(ErrorCode::OutOfRange,
                              "sprite " + std::to_string(e.index) + " does not fit into an atlas page");
        }
        if (k >= batch_end) {
            batch_first = k;
            batch_end = k;
            std::size_t bytes = 0;
            while (batch_end < entries.size() && batch_end - batch_first < kBatchSprites &&
                   (bytes == 0 || bytes < kBatchBytes)) {
                bytes += std::size_t{entries[batch_end].width} * entries[batch_end].height * 4;
                ++batch_end;
            }
#ifdef O1602_EXPERIMENTAL_VITA
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
            decode_batch(batch_first, batch_end - batch_first);
        }
        if (valid[k - batch_first] == 0) {
            continue;  // corrupt sprite: drawn as missing
        }
        const std::vector<std::uint8_t>& rgba = decoded[k - batch_first];
        if (atlas.m_pages.empty()) {
            new_page();
        }
        if (shelf_x + e.width + kPad > page_size) {  // next shelf
            shelf_y += shelf_h;
            shelf_x = 0;
            shelf_h = 0;
        }
        if (shelf_y + e.height + kPad > page_size) {
            new_page();
        }
        RgbaImage& page = atlas.m_pages.back();
        if(atlas.m_backing){
            atlas.m_backing->append(e.index,std::span(rgba).first(std::size_t{e.width}*e.height*4),compressed.empty()?std::span<const std::uint8_t>{}:std::span<const std::uint8_t>(compressed[k-batch_first]));
            const auto pixels=std::size_t{e.width}*e.height;
            const auto offset=atlas.m_alpha.size();atlas.m_alpha_offsets[e.index]=static_cast<std::uint32_t>(offset);
            atlas.m_alpha.resize(offset+(pixels+7)/8,0);
            for(std::size_t i=0;i<pixels;++i)if(rgba[i*4+3])atlas.m_alpha[offset+i/8]|=static_cast<std::uint8_t>(1U<<(i%8));
        }
        else for (std::uint32_t y = 0; y < e.height; ++y) {
            const std::size_t src = std::size_t{y} * e.width * 4;
            const std::size_t dst = ((std::size_t{shelf_y} + y) * page_size + shelf_x) * 4;
            std::copy_n(rgba.begin() + static_cast<std::ptrdiff_t>(src), std::size_t{e.width} * 4,
                        page.pixels.begin() + static_cast<std::ptrdiff_t>(dst));
        }
        atlas.m_rects[e.index] = AtlasRect{static_cast<std::uint16_t>(atlas.m_pages.size() - 1),
                                           static_cast<std::uint16_t>(shelf_x), static_cast<std::uint16_t>(shelf_y),
                                           static_cast<std::uint16_t>(e.width), static_cast<std::uint16_t>(e.height)};
        atlas.m_max_width = std::max(atlas.m_max_width, static_cast<std::int32_t>(e.width));
        atlas.m_max_height = std::max(atlas.m_max_height, static_cast<std::int32_t>(e.height));
        const auto work=worked_pixels.fetch_add(std::uint64_t(e.width)*e.height)+std::uint64_t(e.width)*e.height;
        if(progress)progress->set(GraphicsPhase::Building,work,2*total_pixels);
        shelf_x += e.width + kPad;
        shelf_h = std::max(shelf_h, e.height + kPad);
    }
    if(atlas.m_backing&&!atlas.m_pages.empty()){
        if(std::fflush(atlas.m_backing->file)!=0)throw std::runtime_error("cannot flush private atlas cache");
    }
    if(atlas.m_backing)atlas.m_backing->compressor.reset();
    if(progress)progress->set(GraphicsPhase::Saving,0,1);
    if(!prepared.empty())atlas.store_prepared(prepared,cache_key);
    else if(atlas.m_backing){
        // End the buffered append stream before reads. Keeping an stdio read
        // buffer could hide an externally truncated/corrupt cache from checks.
        auto& b=*atlas.m_backing;std::fclose(b.file);b.file=std::fopen(b.path.string().c_str(),"rb");
        if(!b.file)throw std::runtime_error("cannot reopen sprite cache");
        std::setvbuf(b.file,nullptr,_IONBF,0);
    }
    if(progress)progress->set(GraphicsPhase::Saving,1,1);
    return atlas;
}

void SpriteAtlas::drop_pixels() noexcept {
    for (RgbaImage& page : m_pages) {
        std::vector<std::uint8_t>().swap(page.pixels);
    }
}

namespace {

/// Translucent tile diamond: width w, height w/2, bounding box left = x, bottom = y. Hatched, only
/// every second pixel (hatch_pixel at the draw list's x + dx, y + dy) is filled.
void fill_diamond(RgbaImage& target, std::int32_t x, std::int32_t y, std::int32_t w, assets::Rgba colour,
                  std::uint8_t alpha, bool hatched = false, std::int32_t dx = 0, std::int32_t dy = 0) {
    const std::int32_t h = w / 2;
    const std::int32_t top = y - h;
    for (std::int32_t row = 0; row < h; ++row) {
        const std::int32_t ty = top + row;
        if (ty < 0 || ty >= static_cast<std::int32_t>(target.height)) {
            continue;
        }
        // Half width at this row: widest in the middle.
        const std::int32_t d = row < h / 2 ? row : h - 1 - row;
        const std::int32_t half = (d + 1) * 2;
        const std::int32_t cx = x + w / 2;
        const std::int32_t x0 = std::max(0, cx - half);
        const std::int32_t x1 = std::min(static_cast<std::int32_t>(target.width), cx + half);
        for (std::int32_t tx = x0; tx < x1; ++tx) {
            if (hatched && !hatch_pixel(tx + dx, ty + dy)) {
                continue;
            }
            std::uint8_t* p =
                &target.pixels[(static_cast<std::size_t>(ty) * target.width + static_cast<std::size_t>(tx)) * 4];
            p[0] = static_cast<std::uint8_t>((p[0] * (255 - alpha) + colour.r * alpha) / 255);
            p[1] = static_cast<std::uint8_t>((p[1] * (255 - alpha) + colour.g * alpha) / 255);
            p[2] = static_cast<std::uint8_t>((p[2] * (255 - alpha) + colour.b * alpha) / 255);
        }
    }
}

}  // namespace

void blit_draw_list(const std::vector<DrawCommand>& commands, const SpriteAtlas& atlas, RgbaImage& target,
                    std::int32_t origin_x, std::int32_t origin_y) {
    const auto tw = static_cast<std::int32_t>(target.width);
    const auto th = static_cast<std::int32_t>(target.height);
    const std::uint32_t ps = atlas.page_size();
    for (const DrawCommand& c : commands) {
        if (c.sprite == DrawCommand::kOverlayDiamond) {
            fill_diamond(target, c.x - origin_x, c.y - origin_y, c.tint_size, c.tint, c.tint.a);
            continue;
        }
        if (c.sprite == DrawCommand::kOverlayHatch) {
            fill_diamond(target, c.x - origin_x, c.y - origin_y, c.tint_size, c.tint, c.tint.a, true, origin_x,
                         origin_y);
            continue;
        }
        if (c.sprite == DrawCommand::kOverlayOutline) {
            for_each_outline_step(c, [&](std::int32_t x, std::int32_t y) {
                const std::int32_t ty = c.y - origin_y + y;
                if (ty < 0 || ty >= th) {
                    return;
                }
                for (std::int32_t tx = c.x - origin_x + x; tx < c.x - origin_x + x + 2; ++tx) {
                    if (tx >= 0 && tx < tw) {
                        std::uint8_t* p =
                            &target
                                 .pixels[(static_cast<std::size_t>(ty) * target.width + static_cast<std::size_t>(tx)) *
                                         4];
                        p[0] = c.tint.r;
                        p[1] = c.tint.g;
                        p[2] = c.tint.b;
                    }
                }
            });
            continue;
        }
        const AtlasRect& r = atlas.rect(c.sprite);
        if (r.empty()) {
            continue;
        }
        const std::int32_t left = c.x - origin_x;
        const std::int32_t top = c.y - r.height - origin_y;
        const std::int32_t x0 = std::max(0, -left);
        const std::int32_t y0 = std::max(0, -top);
        const std::int32_t x1 = std::min<std::int32_t>(r.width, tw - left);
        const std::int32_t y1 =
            std::min<std::int32_t>(r.height - std::min<std::int32_t>(c.cut_bottom, r.height), th - top);
        const std::vector<std::uint8_t>& page = atlas.pages()[r.page].pixels;
        if (page.empty()) {
            continue;  // pixels were dropped
        }
        const bool shadow = c.mode == DrawCommand::Mode::Shadow;
        const bool translucent = c.mode == DrawCommand::Mode::Translucent;
        const bool dimmed = c.mode == DrawCommand::Mode::Dimmed;
        const bool tinted = c.mode == DrawCommand::Mode::Tinted;
        const bool checkered = c.mode == DrawCommand::Mode::Checkered;
        const std::uint32_t alpha = c.tint.a;
        for (std::int32_t y = y0; y < y1; ++y) {
            const std::uint8_t* src = &page[((std::size_t{r.y} + static_cast<std::size_t>(y)) * ps + r.x) * 4];
            std::uint8_t* dst = &target.pixels[(static_cast<std::size_t>(top + y) * target.width) * 4];
            for (std::int32_t x = x0; x < x1; ++x) {
                const std::uint8_t* s = src + static_cast<std::size_t>(x) * 4;
                if (s[3] == 0) {
                    continue;
                }
                std::uint8_t* d = dst + static_cast<std::size_t>(left + x) * 4;
                if (shadow) {
                    d[0] = static_cast<std::uint8_t>((d[0] * (255 - alpha) + c.tint.r * alpha) / 255);
                    d[1] = static_cast<std::uint8_t>((d[1] * (255 - alpha) + c.tint.g * alpha) / 255);
                    d[2] = static_cast<std::uint8_t>((d[2] * (255 - alpha) + c.tint.b * alpha) / 255);
                } else if (translucent) {
                    for (int k = 0; k < 3; ++k) {
                        d[k] = static_cast<std::uint8_t>((d[k] * (255 - alpha) + s[k] * alpha) / 255);
                    }
                } else if (tinted) {
                    const std::array<std::uint32_t, 3> t{c.tint.r, c.tint.g, c.tint.b};
                    for (std::size_t k = 0; k < 3; ++k) {
                        const std::uint32_t coloured = s[k] * t[k] / 255;
                        d[k] = static_cast<std::uint8_t>((d[k] * (255 - alpha) + coloured * alpha) / 255);
                    }
                } else if (dimmed) {
                    d[0] = static_cast<std::uint8_t>(s[0] * c.tint.r / 255);
                    d[1] = static_cast<std::uint8_t>(s[1] * c.tint.g / 255);
                    d[2] = static_cast<std::uint8_t>(s[2] * c.tint.b / 255);
                    d[3] = s[3];
                } else if (checkered && hatch_pixel(c.x + x, c.y - r.height + y)) {
                    d[0] = c.tint.r;
                    d[1] = c.tint.g;
                    d[2] = c.tint.b;
                    d[3] = s[3];
                } else {
                    std::copy_n(s, 4, d);
                }
            }
        }
    }
}

}  // namespace o1602::render
