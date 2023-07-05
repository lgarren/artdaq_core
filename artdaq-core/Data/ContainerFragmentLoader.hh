#ifndef artdaq_core_Data_ContainerFragmentLoader_hh
#define artdaq_core_Data_ContainerFragmentLoader_hh

////////////////////////////////////////////////////////////////////////
// ContainerFragmentLoader
//
// This class gives write access to a ContainerFragment. It should be
// used when multiple fragments are generated by one BoardReader for a
// single event.
//
////////////////////////////////////////////////////////////////////////

#include "artdaq-core/Data/ContainerFragment.hh"
#include "artdaq-core/Data/Fragment.hh"

#include "TRACE/tracemf.h"

#include <iostream>

namespace artdaq {
class ContainerFragmentLoader;
}

/**
 * \brief A Read-Write version of the ContainerFragment, used for filling ContainerFragment objects with other Fragment objects
 */
class artdaq::ContainerFragmentLoader : public artdaq::ContainerFragment
{
public:
	/**
	 * \brief Constructs the ContainerFragmentLoader
	 * \param f A Fragment object containing a Fragment header.
	 * \param expectedFragmentType The type of fragment which will be put into this ContainerFragment
	 * \exception cet::exception if the Fragment input has inconsistent Header information
	 */
	explicit ContainerFragmentLoader(Fragment& f, Fragment::type_t expectedFragmentType);

	// ReSharper disable once CppMemberFunctionMayBeConst
	/**
	 * \brief Get the ContainerFragment metadata (includes information about the location of Fragment objects within the ContainerFragment)
	 * \return The ContainerFragment metadata
	 */
	Metadata* metadata()
	{
		assert(artdaq_Fragment_.hasMetadata());
		return reinterpret_cast<Metadata*>(&*artdaq_Fragment_.metadataAddress());  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	}

	/**
	 * \brief Sets the type of Fragment which this ContainerFragment should contain
	 * \param type The Fragment::type_t identifying the type of Fragment objects stored in this ContainerFragment
	 */
	void set_fragment_type(Fragment::type_t type)
	{
		metadata()->fragment_type = type;
	}

	/**
	 * \brief Sets the missing_data flag
	 * \param isDataMissing The value of the missing_data flag
	 *
	 * The ContainerFragment::Metadata::missing_data flag is used for FragmentGenerators to indicate that the fragment is incomplete,
	 * but the generator does not have the correct data to fill it. This happens for Window-mode FragmentGenerators when the window
	 * requested is before the start of the FragmentGenerator's buffers, for example.
	 */
	void set_missing_data(bool isDataMissing)
	{
		metadata()->missing_data = isDataMissing;
	}

	/**
	 * \brief Add a Fragment to the ContainerFragment by reference
	 * \param frag A Fragment object to be added to the ContainerFragment
	 * \exception cet::exception If the Fragment to be added has a different type than expected
	 */
	void addFragment(artdaq::Fragment& frag, bool allowDifferentTypes = false);

	/**
	 * \brief Add a Fragment to the ContainerFragment by smart pointer
	 * \param frag A FragmentPtr to a Fragment to be added to the ContainerFragment
	 */
	void addFragment(artdaq::FragmentPtr& frag, bool allowDifferentTypes = false);

	/**
	 * \brief Add a collection of Fragment objects to the ContainerFragment
	 * \param frags An artdaq::FragmentPtrs object containing Fragments to be added to the ContainerFragment
	 */
	void addFragments(artdaq::FragmentPtrs& frags, bool allowDifferentTypes = false);

private:
	// Note that this non-const reference hides the const reference in the base class
	artdaq::Fragment& artdaq_Fragment_;

	static size_t words_to_frag_words_(size_t nWords);

	void addSpace_(size_t bytes);

	uint8_t* dataBegin_() { return reinterpret_cast<uint8_t*>(&*artdaq_Fragment_.dataBegin()); }  // NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)
	void* dataEnd_() { return static_cast<void*>(dataBegin_() + lastFragmentIndex()); }           // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
};

inline artdaq::ContainerFragmentLoader::ContainerFragmentLoader(artdaq::Fragment& f, artdaq::Fragment::type_t expectedFragmentType = Fragment::EmptyFragmentType)
    : ContainerFragment(f)
    , artdaq_Fragment_(f)
{
	artdaq_Fragment_.setSystemType(Fragment::ContainerFragmentType);
	Metadata m;
	m.block_count = 0;
	m.fragment_type = expectedFragmentType;
	m.missing_data = false;
	m.has_index = true;
	m.version = ContainerFragment::CURRENT_VERSION;
	m.index_offset = 0;
	artdaq_Fragment_.setMetadata<Metadata>(m);

	if (artdaq_Fragment_.size() !=
	    artdaq::detail::RawFragmentHeader::num_words() +
	        words_to_frag_words_(Metadata::size_words))
	{
		TLOG(TLVL_ERROR, "ContainerFragmentLoader") << "ContainerFragmentLoader: Raw artdaq::Fragment object size suggests it does not consist of its own header + the ContainerFragment::Metadata object";
		TLOG(TLVL_ERROR, "ContainerFragmentLoader") << "artdaq_Fragment size: " << artdaq_Fragment_.size() << ", Expected size: " << artdaq::detail::RawFragmentHeader::num_words() + words_to_frag_words_(Metadata::size_words);

		throw cet::exception("InvalidFragment") << "ContainerFragmentLoader: Raw artdaq::Fragment object size suggests it does not consist of its own header + the ContainerFragment::Metadata object";  // NOLINT(cert-err60-cpp)
	}

	artdaq_Fragment_.resize(1);
	*artdaq_Fragment_.dataBegin() = CONTAINER_MAGIC;
}

inline size_t artdaq::ContainerFragmentLoader::words_to_frag_words_(size_t nWords)
{
	size_t mod = nWords % words_per_frag_word_();
	return mod ? nWords / words_per_frag_word_() + 1 : nWords / words_per_frag_word_();
}

inline void artdaq::ContainerFragmentLoader::addSpace_(size_t bytes)
{
	auto currSize = sizeof(artdaq::Fragment::value_type) * artdaq_Fragment_.dataSize();  // Resize takes into account header and metadata size
	artdaq_Fragment_.resizeBytesWithCushion(bytes + currSize, 1.3);
	reset_index_ptr_();  // Must reset index_ptr after resize operation!

	TLOG(TLVL_DEBUG + 33, "ContainerFragmentLoader") << "addSpace_: dataEnd_ is now at " << static_cast<void*>(dataEnd_()) << " (oldSizeBytes/deltaBytes: " << currSize << "/" << bytes << ")";
}

inline void artdaq::ContainerFragmentLoader::addFragment(artdaq::Fragment& frag, bool allowDifferentTypes)
{
	TLOG(TLVL_DEBUG + 33, "ContainerFragmentLoader") << "addFragment: Adding Fragment with payload size " << frag.dataSizeBytes() << " to Container";
	if (metadata()->fragment_type == Fragment::EmptyFragmentType)
		metadata()->fragment_type = frag.type();
	else if (!allowDifferentTypes && frag.type() != metadata()->fragment_type)
	{
		TLOG(TLVL_ERROR, "ContainerFragmentLoader") << "addFragment: Trying to add a fragment of different type than what's already been added!";
		throw cet::exception("WrongFragmentType") << "ContainerFragmentLoader::addFragment: Trying to add a fragment of different type than what's already been added!";  // NOLINT(cert-err60-cpp)
	}

	TLOG(TLVL_DEBUG + 33, "ContainerFragmentLoader") << "addFragment: Payload Size is " << artdaq_Fragment_.dataSizeBytes() << ", lastFragmentIndex is " << lastFragmentIndex() << ", and frag.size is " << frag.sizeBytes();
	if (artdaq_Fragment_.dataSizeBytes() < (lastFragmentIndex() + frag.sizeBytes() + sizeof(size_t) * (metadata()->block_count + 2)))
	{
		addSpace_((lastFragmentIndex() + frag.sizeBytes() + sizeof(size_t) * (metadata()->block_count + 2)) - artdaq_Fragment_.dataSizeBytes());
	}
	//frag.setSequenceID(artdaq_Fragment_.sequenceID());
	TLOG(TLVL_DEBUG + 33, "ContainerFragmentLoader") << "addFragment, copying " << frag.sizeBytes() << " bytes from " << static_cast<void*>(frag.headerAddress()) << " to " << static_cast<void*>(dataEnd_());
	memcpy(dataEnd_(), frag.headerAddress(), frag.sizeBytes());
	metadata()->has_index = 0;

	metadata()->block_count++;

	auto index = create_index_();
	metadata()->index_offset = index[metadata()->block_count - 1];                                           // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	memcpy(dataBegin_() + metadata()->index_offset, index, sizeof(size_t) * (metadata()->block_count + 1));  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

	metadata()->has_index = 1;
	reset_index_ptr_();
}

inline void artdaq::ContainerFragmentLoader::addFragment(artdaq::FragmentPtr& frag, bool allowDifferentTypes)
{
	addFragment(*frag, allowDifferentTypes);
}

inline void artdaq::ContainerFragmentLoader::addFragments(artdaq::FragmentPtrs& frags, bool allowDifferentTypes)
{
	TLOG(TLVL_DEBUG + 33, "ContainerFragmentLoader") << "addFragments: Adding " << frags.size() << " Fragments to Container";

	size_t total_size = 0;
	for (auto& frag : frags) { total_size += frag->sizeBytes(); }

	TLOG(TLVL_DEBUG + 33, "ContainerFragmentLoader") << "addFragments: Payload Size is " << artdaq_Fragment_.dataSizeBytes() << ", lastFragmentIndex is " << lastFragmentIndex() << ", and size to add is " << total_size;
	if (artdaq_Fragment_.dataSizeBytes() < (lastFragmentIndex() + total_size + sizeof(size_t) * (metadata()->block_count + 1 + frags.size())))
	{
		addSpace_((lastFragmentIndex() + total_size + sizeof(size_t) * (metadata()->block_count + 1 + frags.size())) - artdaq_Fragment_.dataSizeBytes());
	}

	auto data_ptr = dataEnd_();

	for (auto& frag : frags)
	{
		if (metadata()->fragment_type == Fragment::EmptyFragmentType)
			metadata()->fragment_type = frag->type();
		else if (!allowDifferentTypes && frag->type() != metadata()->fragment_type)
		{
			TLOG(TLVL_ERROR, "ContainerFragmentLoader") << "addFragments: Trying to add a fragment of different type than what's already been added!";
			throw cet::exception("WrongFragmentType") << "ContainerFragmentLoader::addFragments: Trying to add a fragment of different type than what's already been added!";  // NOLINT(cert-err60-cpp)
		}

		//frag->setSequenceID(artdaq_Fragment_.sequenceID());
		TLOG(TLVL_DEBUG + 33, "ContainerFragmentLoader") << "addFragments, copying " << frag->sizeBytes() << " bytes from " << static_cast<void*>(frag->headerAddress()) << " to " << static_cast<void*>(dataEnd_());
		memcpy(data_ptr, frag->headerAddress(), frag->sizeBytes());
		data_ptr = static_cast<uint8_t*>(data_ptr) + frag->sizeBytes();
	}
	metadata()->has_index = 0;
	metadata()->block_count += frags.size();

	auto index = create_index_();
	metadata()->index_offset = index[metadata()->block_count - 1];                                           // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
	memcpy(dataBegin_() + metadata()->index_offset, index, sizeof(size_t) * (metadata()->block_count + 1));  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)

	metadata()->has_index = 1;
	reset_index_ptr_();
}

#endif /* artdaq_core_Data_ContainerFragmentLoader_hh */
