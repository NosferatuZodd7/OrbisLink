// SPDX-License-Identifier: LGPL-3.0-only
#include "orbislink/fpkg/pfs_builder.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <deque>
#include <set>
#include <stdexcept>
#include <thread>
#include <unordered_map>

namespace orbislink::fpkg {

// ───────────────────────────────── tree

std::string FsFile::fullPath() const
{
	return parent ? parent->fullPath() + "/" + name : "/" + name;
}

std::string FsDir::fullPath() const
{
	// The root has no name of its own in paths.
	return parent ? parent->fullPath() + "/" + name : std::string();
}

FsDir *FsDir::childDir(const std::string &dirName)
{
	for(auto &d : dirs)
		if(d->name == dirName)
			return d.get();
	return nullptr;
}

FsFile *FsDir::findFile(const std::string &path)
{
	const size_t slash = path.find('/');
	if(slash == std::string::npos)
	{
		for(auto &f : files)
			if(f->name == path)
				return f.get();
		return nullptr;
	}
	FsDir *d = childDir(path.substr(0, slash));
	return d ? d->findFile(path.substr(slash + 1)) : nullptr;
}

FsDir *FsDir::makeDirs(const std::string &path)
{
	FsDir *d = this;
	size_t start = 0;
	while(start < path.size())
	{
		size_t slash = path.find('/', start);
		if(slash == std::string::npos)
			slash = path.size();
		const std::string part = path.substr(start, slash - start);
		if(!part.empty())
		{
			FsDir *next = d->childDir(part);
			if(!next)
			{
				auto created = std::make_unique<FsDir>();
				created->name = part;
				created->parent = d;
				next = created.get();
				d->dirs.push_back(std::move(created));
			}
			d = next;
		}
		start = slash + 1;
	}
	return d;
}

uint32_t pkgEntryIdForName(const std::string &name)
{
	static const std::map<std::string, uint32_t> names = [] {
		std::map<std::string, uint32_t> m = {
			{"license.dat", 0x400}, {"license.info", 0x401}, {"nptitle.dat", 0x402},
			{"npbind.dat", 0x403}, {"selfinfo.dat", 0x404}, {"imageinfo.dat", 0x406},
			{"target-deltainfo.dat", 0x407}, {"origin-deltainfo.dat", 0x408},
			{"psreserved.dat", 0x409}, {"param.sfo", 0x1000}, {"playgo-chunk.dat", 0x1001},
			{"playgo-chunk.sha", 0x1002}, {"playgo-manifest.xml", 0x1003},
			{"pronunciation.xml", 0x1004}, {"pronunciation.sig", 0x1005}, {"pic1.png", 0x1006},
			{"pubtoolinfo.dat", 0x1007}, {"shareparam.json", 0x100B},
			{"shareoverlayimage.png", 0x100C}, {"save_data.png", 0x100D},
			{"shareprivacyguardimage.png", 0x100E}, {"icon0.png", 0x1200}, {"pic0.png", 0x1220},
			{"snd0.at9", 0x1240}, {"icon0.dds", 0x1280}, {"pic0.dds", 0x12A0},
			{"pic1.dds", 0x12C0},
		};
		char buffer[32];
		for(int i = 0; i < 31; ++i)
		{
			std::snprintf(buffer, sizeof buffer, "icon0_%02d.png", i);
			m[buffer] = 0x1201 + i;
			std::snprintf(buffer, sizeof buffer, "icon0_%02d.dds", i);
			m[buffer] = 0x1281 + i;
			std::snprintf(buffer, sizeof buffer, "pic1_%02d.png", i);
			m[buffer] = 0x1241 + i;
			std::snprintf(buffer, sizeof buffer, "pic1_%02d.dds", i);
			m[buffer] = 0x12C1 + i;
		}
		return m;
	}();
	const auto it = names.find(name);
	return it == names.end() ? 0 : it->second;
}

// ───────────────────────────────── layout

namespace {

constexpr uint16_t kModeRx = 1 | 4 | 8 | 32 | 64 | 256; // r-x for everyone
constexpr uint16_t kModeDir = 16384;
constexpr uint16_t kModeFile = 32768;
constexpr uint32_t kFlagCompressed = 0x1;
constexpr uint32_t kFlagUnk2 = 0x4;
constexpr uint32_t kFlagUnk3 = 0x8;
constexpr uint32_t kFlagReadonly = 0x10;
constexpr uint32_t kFlagInternal = 0x20000;
constexpr int32_t kDirentFile = 2, kDirentDir = 3, kDirentDot = 4, kDirentDotDot = 5;
constexpr int kDirentMaxSize = 280;
constexpr uint64_t kSignedInodeSize = 0x2C8;
constexpr uint64_t kPlainInodeSize = 0xA8;
constexpr uint32_t kSectorSize = 0x1000;

struct Inode
{
	uint32_t number = 0;
	uint16_t mode = 0;
	uint16_t nlink = 1;
	uint32_t flags = 0;
	int64_t size = 0;
	int64_t sizeCompressed = 0;
	uint32_t blocks = 0;
	int32_t db[12] = {};
};

struct Dirent
{
	uint32_t inode = 0;
	int32_t type = 0;
	std::string name;
	int entSize() const
	{
		int s = static_cast<int>(name.size()) + 17;
		if(s % 8 != 0)
			s += 8 - s % 8;
		return s;
	}
};

struct Sig
{
	uint64_t block;
	uint64_t offset;
	uint32_t size;
};

struct Node
{
	FsDir *dir = nullptr;
	FsFile *file = nullptr;
	Inode *ino = nullptr;
	std::string path() const { return dir ? dir->fullPath() : file->fullPath(); }
};

struct Extent
{
	uint64_t start;
	uint64_t count;
	FsFile *file;
};

uint32_t pathHash(const std::string &name)
{
	uint32_t hash = 0;
	for(char c : name)
	{
		const unsigned char u = static_cast<unsigned char>(c);
		const uint32_t up = (u >= 'a' && u <= 'z') ? u - 32 : u;
		hash = up + 31 * hash;
	}
	return hash;
}

template<typename T> void putLe(uint8_t *p, T v)
{
	for(size_t i = 0; i < sizeof(T); ++i)
		p[i] = static_cast<uint8_t>(static_cast<uint64_t>(v) >> (8 * i));
}

uint64_t ceilDiv(uint64_t a, uint64_t b) { return a / b + (a % b == 0 ? 0 : 1); }

// Writes into a sparse set of blocks as if it were one stream.
class BlockWriter
{
public:
	BlockWriter(std::map<uint64_t, Bytes> &blocks, uint32_t blockSize)
		: blocks_(blocks), blockSize_(blockSize)
	{
	}
	uint64_t pos = 0;
	void write(const void *data, size_t size)
	{
		const uint8_t *p = static_cast<const uint8_t *>(data);
		while(size > 0)
		{
			const uint64_t block = pos / blockSize_;
			const size_t within = static_cast<size_t>(pos % blockSize_);
			const size_t n = std::min<size_t>(size, blockSize_ - within);
			Bytes &b = blocks_[block];
			if(b.empty())
				b.assign(blockSize_, 0);
			std::memcpy(b.data() + within, p, n);
			p += n;
			size -= n;
			pos += n;
		}
	}
	template<typename T> void le(T v)
	{
		uint8_t buffer[sizeof(T)];
		putLe(buffer, v);
		write(buffer, sizeof buffer);
	}
	void zeros(size_t n)
	{
		static const uint8_t z[64] = {};
		while(n > 0)
		{
			const size_t k = std::min<size_t>(n, sizeof z);
			write(z, k);
			n -= k;
		}
	}
	void touch(uint64_t block)
	{
		Bytes &b = blocks_[block];
		if(b.empty())
			b.assign(blockSize_, 0);
	}

private:
	std::map<uint64_t, Bytes> &blocks_;
	uint32_t blockSize_;
};

} // namespace

struct PfsLayout
{
	std::deque<Inode> inodes; // stable addresses
	std::vector<Inode *> inodeOrder;
	Inode *superRoot = nullptr, *fpt = nullptr, *cr = nullptr, *uroot = nullptr;
	std::map<const FsDir *, Inode *> dirInodes;
	std::map<const FsDir *, std::vector<Dirent>> dirents;
	std::map<const FsFile *, Inode *> fileInodes;
	std::vector<Dirent> superRootDirents;
	std::map<uint32_t, uint32_t> fptMap;
	std::vector<std::vector<Dirent>> collisions;
	uint64_t collisionSize = 0;

	std::vector<Node> nodes; // root first, then dirs (sorted), then files
	std::vector<Sig> dataSigs;
	std::vector<Sig> finalSigs; // in push order; processed last to first
	std::vector<Extent> extents;
	std::map<uint64_t, Bytes> meta;

	int64_t ndblock = 0;
	int64_t dinodeCount = 0;
	int64_t dinodeBlockCount = 0;
	int64_t emptyBlock = 4;
	// The header's own inode for the inode blocks.
	uint32_t headerInodeBlocks = 0;
	int64_t headerInodeSize = 0;
	uint32_t headerInodeFlags = kFlagReadonly;
	int32_t headerInodeDb[12] = {};

	Inode *addInode(const PfsOptions &o, uint16_t mode, uint32_t blocks, int64_t size,
		int64_t sizeCompressed, uint16_t nlink, uint32_t number, uint32_t flags)
	{
		inodes.emplace_back();
		Inode &i = inodes.back();
		i.mode = mode;
		i.blocks = blocks;
		i.size = size;
		i.sizeCompressed = sizeCompressed;
		i.nlink = nlink;
		i.number = number;
		i.flags = o.sign ? (flags | kFlagUnk2 | kFlagUnk3) : flags;
		return &i;
	}
};

namespace {

// The order LibOrbisPkg walks a tree in: a directory's children first, then
// each child's own descendants.
void allChildrenDirs(FsDir &dir, std::vector<FsDir *> &out)
{
	for(auto &d : dir.dirs)
		out.push_back(d.get());
	for(auto &d : dir.dirs)
		allChildrenDirs(*d, out);
}

uint64_t nodeSize(const PfsLayout &l, const Node &n)
{
	if(n.file)
		return n.file->size;
	uint64_t s = 0;
	for(const Dirent &d : l.dirents.at(n.dir))
		s += static_cast<uint64_t>(d.entSize());
	return s;
}

} // namespace

PfsImage::PfsImage(FsDir &root, PfsOptions options)
	: root_(root), options_(std::move(options)), layout_(std::make_unique<PfsLayout>())
{
	PfsLayout &l = *layout_;
	const PfsOptions &o = options_;
	const uint64_t bs = o.blockSize;

	l.finalSigs.push_back({0, 0x380, 0x5A0});

	std::vector<FsDir *> allDirs;
	allChildrenDirs(root_, allDirs);
	std::vector<FsFile *> allFiles;
	for(auto &f : root_.files)
		allFiles.push_back(f.get());
	for(FsDir *d : allDirs)
		for(auto &f : d->files)
			if(d->name != "sce_sys" || pkgEntryIdForName(f->name) == 0)
				allFiles.push_back(f.get());

	std::vector<FsDir *> dirsSorted = allDirs;
	std::sort(dirsSorted.begin(), dirsSorted.end(),
		[](FsDir *a, FsDir *b) { return a->fullPath() < b->fullPath(); });

	std::vector<Node> pathNodes;
	for(FsDir *d : dirsSorted)
		pathNodes.push_back({d, nullptr, nullptr});
	for(FsFile *f : allFiles)
		pathNodes.push_back({nullptr, f, nullptr});

	bool hasCollision = false;
	{
		std::set<uint32_t> seen;
		for(const Node &n : pathNodes)
			if(!seen.insert(pathHash(n.path())).second)
				hasCollision = true;
	}

	// ── super root, flat_path_table, collision_resolver, uroot
	uint32_t number = 0;
	l.superRoot = l.addInode(o, kModeDir | kModeRx, 1, 65536, 65536, 1, number++,
		kFlagInternal | kFlagReadonly);
	l.inodeOrder.push_back(l.superRoot);
	l.fpt = l.addInode(o, kModeFile | kModeRx, 1, 0, 0, 1, number++, kFlagInternal | kFlagReadonly);
	l.inodeOrder.push_back(l.fpt);
	if(hasCollision)
	{
		l.cr = l.addInode(o, kModeFile | kModeRx, 1, 0, 0, 1, number++, kFlagInternal | kFlagReadonly);
		l.inodeOrder.push_back(l.cr);
	}
	l.uroot = l.addInode(o, kModeDir | kModeRx, 1, 65536, 65536, 3, number++, kFlagReadonly);
	l.superRootDirents.push_back({l.fpt->number, kDirentFile, "flat_path_table"});
	if(hasCollision)
		l.superRootDirents.push_back({l.cr->number, kDirentFile, "collision_resolver"});
	l.superRootDirents.push_back({l.uroot->number, kDirentDir, "uroot"});
	l.dirInodes[&root_] = l.uroot;
	l.dirents[&root_] = {{l.uroot->number, kDirentDot, "."}, {l.uroot->number, kDirentDotDot, ".."}};
	if(o.sign)
	{
		l.superRoot->flags &= ~kFlagReadonly;
		l.fpt->flags &= ~kFlagReadonly;
		l.uroot->flags &= ~kFlagReadonly;
	}

	// ── directories
	l.inodeOrder.push_back(l.uroot);
	for(FsDir *d : dirsSorted)
	{
		Inode *ino = l.addInode(o, kModeDir | kModeRx, 1, 65536, 0, 2,
			static_cast<uint32_t>(l.inodeOrder.size()), kFlagReadonly);
		l.dirInodes[d] = ino;
		Inode *parent = l.dirInodes.at(d->parent);
		l.dirents[d] = {{ino->number, kDirentDot, "."}, {parent->number, kDirentDotDot, ".."}};
		l.dirents[d->parent].push_back({ino->number, kDirentDir, d->name});
		parent->nlink++;
		l.inodeOrder.push_back(ino);
	}

	// ── files
	std::vector<FsFile *> filesSorted = allFiles;
	std::sort(filesSorted.begin(), filesSorted.end(),
		[](FsFile *a, FsFile *b) { return a->fullPath() < b->fullPath(); });
	for(FsFile *f : filesSorted)
	{
		Inode *ino = l.addInode(o, kModeFile | kModeRx, static_cast<uint32_t>(ceilDiv(f->size, bs)),
			static_cast<int64_t>(f->size), static_cast<int64_t>(f->compressedSize), 1,
			static_cast<uint32_t>(l.inodeOrder.size()),
			kFlagReadonly | (f->compressed ? kFlagCompressed : 0));
		if(o.sign)
			ino->flags &= ~kFlagReadonly;
		l.fileInodes[f] = ino;
		l.dirents[f->parent].push_back({ino->number, kDirentFile, f->name});
		l.inodeOrder.push_back(ino);
	}

	for(Node &n : pathNodes)
		n.ino = n.dir ? l.dirInodes.at(n.dir) : l.fileInodes.at(n.file);

	// ── flat_path_table (and the resolver for names that hash alike)
	{
		std::map<uint32_t, std::vector<const Node *>> byHash;
		bool collision = false;
		for(const Node &n : pathNodes)
		{
			const uint32_t h = pathHash(n.path());
			auto it = l.fptMap.find(h);
			if(it != l.fptMap.end())
			{
				it->second = 0x80000000u;
				byHash[h].push_back(&n);
				collision = true;
			}
			else
			{
				l.fptMap[h] = n.ino->number | (n.dir ? 0x20000000u : 0u);
				byHash[h] = {&n};
			}
		}
		if(collision)
		{
			uint32_t offset = 0;
			for(auto &kv : l.fptMap)
			{
				if(kv.second != 0x80000000u)
					continue;
				kv.second = 0x80000000u | offset;
				std::vector<Dirent> list;
				for(const Node *n : byHash[kv.first])
				{
					Dirent d {n->ino->number, n->dir ? kDirentDir : kDirentFile, n->path()};
					offset += static_cast<uint32_t>(d.entSize());
					list.push_back(std::move(d));
				}
				offset += 0x18;
				l.collisions.push_back(std::move(list));
			}
			for(const auto &list : l.collisions)
			{
				for(const Dirent &d : list)
					l.collisionSize += static_cast<uint64_t>(d.entSize());
				l.collisionSize += 0x18;
			}
		}
	}
	const uint64_t fptSize = l.fptMap.size() * 8;

	l.nodes.push_back({&root_, nullptr, l.uroot});
	for(const Node &n : pathNodes)
		l.nodes.push_back(n);

	// ── where every block goes
	auto inoOffset = [&](uint32_t num, int db) {
		return bs + kSignedInodeSize * num + 0x64 + 36u * static_cast<uint64_t>(db);
	};
	if(o.sign)
	{
		l.ndblock = 1;
		const int64_t perBlock = static_cast<int64_t>(bs / kSignedInodeSize);
		l.dinodeCount = static_cast<int64_t>(l.inodeOrder.size());
		l.dinodeBlockCount = static_cast<int64_t>(ceilDiv(static_cast<uint64_t>(l.dinodeCount),
			static_cast<uint64_t>(perBlock)));
		l.headerInodeBlocks = static_cast<uint32_t>(l.dinodeBlockCount);
		l.headerInodeSize = l.dinodeBlockCount * static_cast<int64_t>(bs);
		l.headerInodeFlags = 0;
		for(int64_t i = 0; i < l.dinodeBlockCount && i < 12; ++i)
		{
			l.headerInodeDb[i] = static_cast<int32_t>(1 + i);
			l.finalSigs.push_back({static_cast<uint64_t>(1 + i), 0xB8 + 36u * static_cast<uint64_t>(i), 0x10000});
		}
		l.ndblock += l.dinodeBlockCount;
		l.superRoot->db[0] = static_cast<int32_t>(l.dinodeBlockCount + 1);
		l.finalSigs.push_back({static_cast<uint64_t>(l.superRoot->db[0]), inoOffset(l.superRoot->number, 0), 0x10000});
		l.ndblock += l.superRoot->blocks;

		l.fpt->db[0] = l.superRoot->db[0] + 1;
		l.fpt->size = l.fpt->sizeCompressed = static_cast<int64_t>(fptSize);
		l.fpt->blocks = static_cast<uint32_t>(ceilDiv(fptSize, bs));
		l.finalSigs.push_back({static_cast<uint64_t>(l.fpt->db[0]), inoOffset(l.fpt->number, 0), 0x10000});
		for(uint32_t i = 1; i < l.fpt->blocks && i < 12; ++i)
		{
			l.fpt->db[i] = static_cast<int32_t>(l.ndblock++);
			l.finalSigs.push_back({static_cast<uint64_t>(l.fpt->db[0]), inoOffset(l.fpt->number, static_cast<int>(i)), 0x10000});
		}
		l.ndblock++;
		// An empty block follows, and it is never encrypted.
		l.emptyBlock = l.ndblock;
		l.ndblock++;

		const uint64_t sigsPerBlock = bs / 36;
		auto indirectBlocks = [&](uint64_t size) {
			uint64_t blocks = ceilDiv(size, bs);
			uint64_t ib = 0;
			if(blocks > 12)
			{
				blocks -= 12;
				ib++;
			}
			if(blocks > sigsPerBlock)
			{
				blocks -= sigsPerBlock;
				ib += 1 + ceilDiv(blocks, sigsPerBlock);
			}
			return ib;
		};
		int64_t ibStart = l.ndblock;
		for(const Node &n : l.nodes)
			l.ndblock += static_cast<int64_t>(indirectBlocks(nodeSize(l, n)));
		for(int64_t b = ibStart; b < l.ndblock; ++b)
			l.meta[static_cast<uint64_t>(b)].assign(bs, 0);

		for(const Node &n : l.nodes)
		{
			const uint64_t size = nodeSize(l, n);
			const uint64_t blocks = ceilDiv(size, bs);
			n.ino->db[0] = static_cast<int32_t>(l.ndblock);
			n.ino->blocks = static_cast<uint32_t>(blocks);
			n.ino->size = static_cast<int64_t>(n.dir ? blocks * bs : size);
			if(n.ino->sizeCompressed == 0)
				n.ino->sizeCompressed = n.ino->size;
			if(n.file)
				l.extents.push_back({static_cast<uint64_t>(l.ndblock), blocks, n.file});
			for(uint64_t i = 0; i < blocks && i < 12; ++i)
				l.dataSigs.push_back({static_cast<uint64_t>(l.ndblock++), inoOffset(n.ino->number, static_cast<int>(i)), 0x10000});
			if(blocks > 12)
			{
				l.finalSigs.push_back({static_cast<uint64_t>(ibStart), inoOffset(n.ino->number, 12), 0x10000});
				uint64_t pointer = 0;
				for(uint64_t i = 12; i < blocks && i < 12 + sigsPerBlock; ++i, pointer += 36)
					l.dataSigs.push_back({static_cast<uint64_t>(l.ndblock++), static_cast<uint64_t>(ibStart) * bs + pointer, 0x10000});
				ibStart++;
			}
			if(blocks > 12 + sigsPerBlock)
			{
				uint64_t done = 12 + sigsPerBlock;
				l.finalSigs.push_back({static_cast<uint64_t>(ibStart), inoOffset(n.ino->number, 13), 0x10000});
				const int64_t ib1 = ibStart;
				for(uint64_t i = 0; i < sigsPerBlock && done < blocks; ++i)
				{
					++ibStart;
					l.finalSigs.push_back({static_cast<uint64_t>(ibStart), static_cast<uint64_t>(ib1) * bs + i * 36, 0x10000});
					for(uint64_t j = 0; j < sigsPerBlock && done < blocks; ++j, ++done)
						l.dataSigs.push_back({static_cast<uint64_t>(l.ndblock++), static_cast<uint64_t>(ibStart) * bs + j * 36, 0x10000});
				}
			}
		}
	}
	else
	{
		l.ndblock = 1;
		const int64_t perBlock = static_cast<int64_t>(bs / kPlainInodeSize);
		l.dinodeCount = static_cast<int64_t>(l.inodeOrder.size());
		l.dinodeBlockCount = static_cast<int64_t>(ceilDiv(static_cast<uint64_t>(l.dinodeCount),
			static_cast<uint64_t>(perBlock)));
		l.headerInodeBlocks = static_cast<uint32_t>(l.dinodeBlockCount);
		l.headerInodeSize = l.dinodeBlockCount * static_cast<int64_t>(bs);
		l.headerInodeDb[0] = static_cast<int32_t>(l.ndblock++);
		for(int64_t i = 1; i < l.dinodeBlockCount; ++i)
		{
			if(i < 12)
				l.headerInodeDb[i] = -1;
			l.ndblock++;
		}
		l.superRoot->db[0] = static_cast<int32_t>(l.ndblock);
		l.ndblock += l.superRoot->blocks;

		l.fpt->db[0] = static_cast<int32_t>(l.ndblock++);
		l.fpt->size = l.fpt->sizeCompressed = static_cast<int64_t>(fptSize);
		l.fpt->blocks = static_cast<uint32_t>(ceilDiv(fptSize, bs));
		for(uint32_t i = 1; i < l.fpt->blocks && i < 12; ++i)
			l.fpt->db[i] = static_cast<int32_t>(l.ndblock++);
		if(!l.cr)
		{
			l.ndblock++;
		}
		else
		{
			l.cr->db[0] = static_cast<int32_t>(l.ndblock++);
			l.cr->size = l.cr->sizeCompressed = static_cast<int64_t>(l.collisionSize);
			l.cr->blocks = static_cast<uint32_t>(ceilDiv(l.collisionSize, bs));
			for(uint32_t i = 1; i < l.cr->blocks && i < 12; ++i)
				l.cr->db[i] = static_cast<int32_t>(l.ndblock++);
		}
		for(const Node &n : l.nodes)
		{
			const uint64_t size = nodeSize(l, n);
			const uint64_t blocks = ceilDiv(size, bs);
			n.ino->db[0] = static_cast<int32_t>(l.ndblock);
			n.ino->blocks = static_cast<uint32_t>(blocks);
			n.ino->size = static_cast<int64_t>(n.dir ? blocks * bs : size);
			if(n.ino->sizeCompressed == 0)
				n.ino->sizeCompressed = n.ino->size;
			for(uint64_t i = 1; i < blocks && i < 12; ++i)
				n.ino->db[i] = -1;
			if(n.file)
				l.extents.push_back({static_cast<uint64_t>(l.ndblock), blocks, n.file});
			l.ndblock += static_cast<int64_t>(blocks);
		}
	}
	l.ndblock = std::max<int64_t>(l.ndblock, static_cast<int64_t>(o.minBlocks));
	std::sort(l.extents.begin(), l.extents.end(),
		[](const Extent &a, const Extent &b) { return a.start < b.start; });

	// ── render everything that is not file data
	BlockWriter w(l.meta, o.blockSize);
	// Superblock.
	w.pos = 0;
	w.le<int64_t>(1);
	w.le<int64_t>(20130315);
	w.le<int64_t>(0);
	w.le<uint8_t>(0);
	w.le<uint8_t>(0);
	w.le<uint8_t>(1); // read-only
	w.le<uint8_t>(0);
	w.le<uint16_t>(static_cast<uint16_t>((o.sign ? 0x1 : 0) | (o.encrypt ? 0x4 : 0) | 0x8));
	w.le<uint16_t>(0);
	w.le<uint32_t>(o.blockSize);
	w.le<uint32_t>(0);
	w.le<int64_t>(1);
	w.le<int64_t>(l.dinodeCount);
	w.le<int64_t>(l.ndblock);
	w.le<int64_t>(l.dinodeBlockCount);
	w.le<int64_t>(0);
	// The superblock's own 64-bit signed inode, for the inode blocks.
	w.le<uint16_t>(0);
	w.le<uint16_t>(1);
	w.le<uint32_t>(l.headerInodeFlags);
	w.le<int64_t>(l.headerInodeSize);
	w.le<int64_t>(l.headerInodeSize);
	for(int i = 0; i < 4; ++i)
		w.le<int64_t>(o.fileTime);
	w.zeros(16 + 4 + 4 + 8 + 8);
	w.le<uint32_t>(l.headerInodeBlocks);
	w.le<int32_t>(0);
	for(int i = 0; i < 12; ++i)
	{
		w.zeros(32);
		w.le<int64_t>(l.headerInodeDb[i]);
	}
	for(int i = 0; i < 5; ++i)
	{
		w.zeros(32);
		w.le<int64_t>(0);
	}
	const bool hasSeed = o.sign || o.encrypt;
	if(hasSeed)
	{
		w.pos = 0x36C;
		w.le<int32_t>(1);
		w.write(o.seed.data(), o.seed.size());
	}
	else
	{
		w.pos = 0x368;
		w.le<int32_t>(1);
	}

	// Inodes.
	const uint64_t inodeSize = o.sign ? kSignedInodeSize : kPlainInodeSize;
	w.pos = bs;
	for(const Inode *i : l.inodeOrder)
	{
		w.le<uint16_t>(i->mode);
		w.le<uint16_t>(i->nlink);
		w.le<uint32_t>(i->flags);
		w.le<int64_t>(i->size);
		w.le<int64_t>(i->sizeCompressed);
		for(int t = 0; t < 4; ++t)
			w.le<int64_t>(o.fileTime);
		w.zeros(16 + 4 + 4 + 8 + 8);
		w.le<uint32_t>(i->blocks);
		for(int d = 0; d < 12; ++d)
		{
			if(o.sign)
				w.zeros(32);
			w.le<int32_t>(i->db[d]);
		}
		for(int d = 0; d < 5; ++d)
		{
			if(o.sign)
				w.zeros(32);
			w.le<int32_t>(0);
		}
		if(w.pos % bs > bs - inodeSize)
			w.pos += bs - w.pos % bs;
	}
	for(int64_t b = 1; b <= l.dinodeBlockCount; ++b)
		w.touch(static_cast<uint64_t>(b));

	auto writeDirent = [&](const Dirent &d) {
		const uint64_t start = w.pos;
		w.le<uint32_t>(d.inode);
		w.le<int32_t>(d.type);
		w.le<int32_t>(static_cast<int32_t>(d.name.size()));
		w.le<int32_t>(d.entSize());
		w.write(d.name.data(), d.name.size());
		w.zeros(static_cast<size_t>(static_cast<uint64_t>(d.entSize()) - (w.pos - start)));
	};

	// Super root.
	w.pos = bs * static_cast<uint64_t>(l.dinodeBlockCount + 1);
	w.touch(w.pos / bs);
	for(const Dirent &d : l.superRootDirents)
		writeDirent(d);

	// flat_path_table, then the collision resolver.
	w.pos = static_cast<uint64_t>(l.fpt->db[0]) * bs;
	w.touch(w.pos / bs);
	for(const auto &kv : l.fptMap)
	{
		w.le<uint32_t>(kv.first);
		w.le<uint32_t>(kv.second);
	}
	if(l.cr)
	{
		w.pos = static_cast<uint64_t>(l.cr->db[0]) * bs;
		w.touch(w.pos / bs);
		for(const auto &list : l.collisions)
		{
			for(const Dirent &d : list)
				writeDirent(d);
			w.zeros(0x18);
		}
	}

	// Directories.
	for(const Node &n : l.nodes)
	{
		if(!n.dir)
			continue;
		uint64_t block = static_cast<uint64_t>(n.ino->db[0]);
		w.pos = block * bs;
		w.touch(block);
		for(const Dirent &d : l.dirents.at(n.dir))
		{
			writeDirent(d);
			if(w.pos % bs > bs - kDirentMaxSize)
			{
				w.pos = ++block * bs;
				w.touch(block);
			}
		}
	}
	// Blocks a write merely reached are not content of their own.
	for(auto it = l.meta.begin(); it != l.meta.end();)
	{
		if(static_cast<int64_t>(it->first) >= l.ndblock)
			it = l.meta.erase(it);
		else
			++it;
	}
}

PfsImage::~PfsImage() = default;

uint64_t PfsImage::blockCount() const { return static_cast<uint64_t>(layout_->ndblock); }

uint64_t PfsImage::size() const { return blockCount() * options_.blockSize; }

void PfsImage::readBlock(uint64_t block, uint8_t *out) const
{
	const PfsLayout &l = *layout_;
	const uint32_t bs = options_.blockSize;
	const auto m = l.meta.find(block);
	if(m != l.meta.end())
	{
		std::memcpy(out, m->second.data(), bs);
		return;
	}
	auto it = std::upper_bound(l.extents.begin(), l.extents.end(), block,
		[](uint64_t b, const Extent &e) { return b < e.start; });
	if(it != l.extents.begin())
	{
		--it;
		if(block < it->start + it->count)
		{
			const uint64_t offset = (block - it->start) * bs;
			const size_t n = static_cast<size_t>(std::min<uint64_t>(bs, it->file->size - offset));
			it->file->read(offset, out, n);
			if(n < bs)
				std::memset(out + n, 0, bs - n);
			return;
		}
	}
	std::memset(out, 0, bs);
}

void PfsImage::writeFinal(const BlockSink &sink, const std::function<bool(uint64_t)> &progress)
{
	PfsLayout &l = *layout_;
	const uint32_t bs = options_.blockSize;
	if(!options_.sign)
		throw std::logic_error("writeFinal is for signed images");

	const Digest signKey = pfsGenCryptoKey(options_.ekpfs.data(), options_.ekpfs.size(),
		options_.seed.data(), options_.seed.size(), 2);
	const Digest encKey = pfsGenCryptoKey(options_.ekpfs.data(), options_.ekpfs.size(),
		options_.seed.data(), options_.seed.size(), 1);
	const uint8_t *tweakKey = encKey.data();
	const uint8_t *dataKey = encKey.data() + 16;
	const bool encrypt = options_.encrypt;
	const int64_t emptyBlock = l.emptyBlock;

	auto writeSig = [&](uint64_t offset, const Digest &sig, uint64_t block) {
		Bytes &target = l.meta.at(offset / bs);
		const size_t within = static_cast<size_t>(offset % bs);
		std::memcpy(target.data() + within, sig.data(), 32);
		putLe<int32_t>(target.data() + within + 32, static_cast<int32_t>(block));
	};
	auto finish = [&](uint64_t block, uint8_t *data, XtsEncryptor &xts, Sha256 &sha) {
		if(encrypt && block != 0 && static_cast<int64_t>(block) != emptyBlock)
			for(uint32_t s = 0; s < bs / kSectorSize; ++s)
				xts.encryptSector(data + s * kSectorSize, kSectorSize, block * (bs / kSectorSize) + s);
		sha.update(data, bs);
		return sha.finish();
	};

	std::unordered_map<uint64_t, uint64_t> dataSigAt;
	for(const Sig &s : l.dataSigs)
		dataSigAt[s.block] = s.offset;

	uint64_t done = 0;
	auto report = [&](uint64_t bytes) {
		done += bytes;
		if(progress && !progress(done))
			throw std::runtime_error("cancelled");
	};

	// 1. File data: signed, encrypted and handed over in batches, the
	// hashing spread over the processor's cores.
	const unsigned workers = std::max(1u, std::min(8u, std::thread::hardware_concurrency()));
	const uint64_t batchBlocks = 64;
	std::vector<uint8_t> batch(static_cast<size_t>(batchBlocks) * bs);
	std::vector<Digest> sigs(batchBlocks), shas(batchBlocks);
	std::set<uint64_t> fileBlocks;
	for(const Extent &e : l.extents)
	{
		for(uint64_t first = 0; first < e.count; first += batchBlocks)
		{
			const uint64_t count = std::min(batchBlocks, e.count - first);
			for(uint64_t i = 0; i < count; ++i)
			{
				const uint64_t offset = (first + i) * bs;
				const size_t n = static_cast<size_t>(std::min<uint64_t>(bs, e.file->size - offset));
				uint8_t *slot = batch.data() + i * bs;
				e.file->read(offset, slot, n);
				if(n < bs)
					std::memset(slot + n, 0, bs - n);
			}
			std::vector<std::thread> threads;
			for(unsigned t = 0; t < workers; ++t)
			{
				threads.emplace_back([&, t]() {
					XtsEncryptor xts(dataKey, tweakKey);
					Sha256 sha;
					for(uint64_t i = t; i < count; i += workers)
					{
						uint8_t *slot = batch.data() + i * bs;
						sigs[i] = hmacSha256(signKey.data(), signKey.size(), slot, bs);
						shas[i] = finish(e.start + first + i, slot, xts, sha);
					}
				});
			}
			for(auto &th : threads)
				th.join();
			for(uint64_t i = 0; i < count; ++i)
			{
				const uint64_t block = e.start + first + i;
				writeSig(dataSigAt.at(block), sigs[i], block);
				sink(block, batch.data() + i * bs, shas[i]);
			}
			report(count * bs);
		}
		for(uint64_t i = 0; i < e.count; ++i)
			fileBlocks.insert(e.start + i);
	}

	// 2. The data blocks that are metadata (directories), then the
	// signatures over signatures, the last pushed first.
	for(const Sig &s : l.dataSigs)
	{
		if(fileBlocks.count(s.block))
			continue;
		const Bytes &content = l.meta.at(s.block);
		writeSig(s.offset, hmacSha256(signKey.data(), signKey.size(), content.data(), s.size), s.block);
	}
	for(auto it = l.finalSigs.rbegin(); it != l.finalSigs.rend(); ++it)
	{
		const Bytes &content = l.meta.at(it->block);
		writeSig(it->offset, hmacSha256(signKey.data(), signKey.size(), content.data(), it->size), it->block);
	}

	// 3. Everything else: metadata and empty blocks.
	XtsEncryptor xts(dataKey, tweakKey);
	Sha256 sha;
	Bytes buffer(bs);
	for(uint64_t block = 0; block < blockCount(); ++block)
	{
		if(fileBlocks.count(block))
			continue;
		const auto m = l.meta.find(block);
		if(m != l.meta.end())
			std::memcpy(buffer.data(), m->second.data(), bs);
		else
			std::memset(buffer.data(), 0, bs);
		const Digest d = finish(block, buffer.data(), xts, sha);
		sink(block, buffer.data(), d);
		report(bs);
	}
}

std::unique_ptr<FsFile> makePfscFile(const PfsImage &inner)
{
	const uint64_t bs = 0x10000;
	const uint64_t innerSize = inner.size();
	const int64_t numBlocks = static_cast<int64_t>((innerSize + bs - 1) / bs);
	const int64_t pointerTable = 8 + numBlocks * 8;
	const int64_t extra = ((pointerTable - 0xFC00) + 0xFFFF) / 0x10000;
	const uint64_t headerSize = 0x10000 + (extra > 0 ? bs * static_cast<uint64_t>(extra) : 0);

	auto header = std::make_shared<Bytes>(static_cast<size_t>(headerSize), 0);
	uint8_t *h = header->data();
	h[0] = 'P';
	h[1] = 'F';
	h[2] = 'S';
	h[3] = 'C';
	putLe<int32_t>(h + 4, 0);
	putLe<int32_t>(h + 8, 6);
	putLe<int32_t>(h + 12, static_cast<int32_t>(bs));
	putLe<int64_t>(h + 16, static_cast<int64_t>(bs));
	putLe<int64_t>(h + 24, 0x400);
	putLe<int64_t>(h + 32, static_cast<int64_t>(headerSize));
	putLe<int64_t>(h + 40, numBlocks * static_cast<int64_t>(bs));
	for(int64_t i = 0; i <= numBlocks; ++i)
		putLe<int64_t>(h + 0x400 + i * 8, static_cast<int64_t>(headerSize + static_cast<uint64_t>(i) * bs));

	auto file = std::make_unique<FsFile>();
	file->name = "pfs_image.dat";
	file->compressed = true;
	file->compressedSize = innerSize;
	file->size = innerSize + headerSize;
	const PfsImage *image = &inner;
	file->read = [header, headerSize, image](uint64_t offset, uint8_t *out, size_t length) {
		Bytes block(image->blockSize());
		while(length > 0)
		{
			size_t n;
			if(offset < headerSize)
			{
				n = static_cast<size_t>(std::min<uint64_t>(length, headerSize - offset));
				std::memcpy(out, header->data() + offset, n);
			}
			else
			{
				const uint64_t at = offset - headerSize;
				const uint64_t index = at / image->blockSize();
				const size_t within = static_cast<size_t>(at % image->blockSize());
				n = std::min<size_t>(length, image->blockSize() - within);
				image->readBlock(index, block.data());
				std::memcpy(out, block.data() + within, n);
			}
			out += n;
			offset += n;
			length -= n;
		}
	};
	return file;
}

} // namespace orbislink::fpkg
