#pragma once

#include "Core/Core.h"
#include "Core/Platform.h"

#include <string>
#include <vector>

namespace Serialization
{
	// An exception that is thrown for various errors during serialization.
	// Any code using serialization should handle it!
	struct FatalSerializationException
	{
		std::string message;
		FatalSerializationException(std::string&& inMessage)
		: message(std::move(inMessage)) {}
	};

	// An abstract output stream.
	struct OutputStream
	{
		enum { isInput = false };

		OutputStream(): next(nullptr), end(nullptr) {}

		size_t capacity() const { return SIZE_MAX; }
		
		// Advances the stream cursor by numBytes, and returns a pointer to the previous stream cursor.
		inline uint8* advance(size_t numBytes)
		{
			if(next + numBytes > end) { extendBuffer(numBytes); }
			assert(next + numBytes <= end);

			uint8* data = next;
			next += numBytes;
			return data;
		}

	protected:

		uint8* next;
		uint8* end;

		// Called when there isn't enough space in the buffer to hold a write to the stream.
		// Should update next and end to point to a new buffer, and ensure that the new
		// buffer has at least numBytes. May throw FatalSerializationException.
		virtual void extendBuffer(size_t numBytes) = 0;
	};

	// An output stream that writes to an array of bytes.
	struct ArrayOutputStream : public OutputStream
	{
		// Moves the output array from the stream to the caller.
		std::vector<uint8>&& getBytes()
		{
			bytes.resize(next - bytes.data());
			next = nullptr;
			end = nullptr;
			return std::move(bytes);
		}
		
	private:

		std::vector<uint8> bytes;

		virtual void extendBuffer(size_t numBytes)
		{
			const uintp nextIndex = next - bytes.data();

			// Grow the array by larger and larger increments, so the time spent growing
			// the buffer is O(1).
			bytes.resize(std::max((size_t)nextIndex+numBytes,bytes.size() * 7 / 5 + 32));

			next = bytes.data() + nextIndex;
			end = bytes.data() + bytes.size();
		}
		virtual bool canExtendBuffer(size_t numBytes) const { return true; }
	};

	// An abstract input stream.
	struct InputStream
	{
		enum { isInput = true };

		InputStream(const uint8* inNext,const uint8* inEnd): next(inNext), end(inEnd) {}

		virtual size_t capacity() const = 0;
		
		// Advances the stream cursor by numBytes, and returns a pointer to the previous stream cursor.
		inline const uint8* advance(size_t numBytes)
		{
			if(next + numBytes > end) { getMoreData(numBytes); }
			const uint8* data = next;
			next += numBytes;
			return data;
		}

		// Returns a pointer to the current stream cursor, ensuring that there are at least numBytes following it.
		inline const uint8* peek(size_t numBytes)
		{
			if(next + numBytes > end) { getMoreData(numBytes); }
			return next;
		}

	protected:

		const uint8* next;
		const uint8* end;
		
		// Called when there isn't enough space in the buffer to satisfy a read from the stream.
		// Should update next and end to point to a new buffer, and ensure that the new
		// buffer has at least numBytes. May throw FatalSerializationException.
		virtual void getMoreData(size_t numBytes) = 0;
	};

	// An input stream that reads from a contiguous range of memory.
	struct MemoryInputStream : InputStream
	{
		MemoryInputStream(const uint8* begin,size_t numBytes): InputStream(begin,begin+numBytes) {}
		virtual size_t capacity() const { return end - next; }
	private:
		virtual void getMoreData(size_t numBytes) { throw FatalSerializationException("expected data but found end of stream"); }
	};

	// Serialize raw byte sequences.
	FORCEINLINE void serializeBytes(OutputStream& stream,const uint8* bytes,size_t numBytes)
	{ memcpy(stream.advance(numBytes),bytes,numBytes); }
	FORCEINLINE void serializeBytes(InputStream& stream,uint8* bytes,size_t numBytes)
	{ memcpy(bytes,stream.advance(numBytes),numBytes); }
	
	// Serialize basic C++ types.
	template<typename Stream,typename Value>
	FORCEINLINE void serializeNativeValue(Stream& stream,Value& value) { serializeBytes(stream,(uint8*)&value,sizeof(Value)); }

	template<typename Stream> void serialize(Stream& stream,uint8& i) { serializeNativeValue(stream,i); }
	template<typename Stream> void serialize(Stream& stream,uint32& i) { serializeNativeValue(stream,i); }
	template<typename Stream> void serialize(Stream& stream,uint64& i) { serializeNativeValue(stream,i);  }
	template<typename Stream> void serialize(Stream& stream,int8& i) { serializeNativeValue(stream,i); }
	template<typename Stream> void serialize(Stream& stream,int32& i) { serializeNativeValue(stream,i); }
	template<typename Stream> void serialize(Stream& stream,int64& i) { serializeNativeValue(stream,i); }
	template<typename Stream> void serialize(Stream& stream,float32& f) { serializeNativeValue(stream,f); }
	template<typename Stream> void serialize(Stream& stream,float64& f) { serializeNativeValue(stream,f); }

	// LEB128 variable-length integer serialization.
	template<typename Value,size_t maxBits>
	FORCEINLINE void serializeVarInt(OutputStream& stream,Value& inValue,Value minValue,Value maxValue)
	{
		Value value = inValue;

		if(value < minValue || value > maxValue)
		{
			throw FatalSerializationException(std::string("out-of-range value: ") + std::to_string(minValue) + "<=" + std::to_string(value) + "<=" + std::to_string(maxValue));
		}

		bool more = true;
		while(more)
		{
			uint8 outputByte = (uint8)(value&127);
			value >>= 7;
			more = std::is_signed<Value>::value
				? (value != 0 && value != Value(-1)) || (value >= 0 && (outputByte & 0x40)) || (value < 0 && !(outputByte & 0x40))
				: (value != 0);
			if(more) { outputByte |= 0x80; }
			*stream.advance(1) = outputByte;
		};
	}
	
	template<typename Value,size_t maxBits>
	FORCEINLINE void serializeVarInt(InputStream& stream,Value& value,Value minValue,Value maxValue)
	{
		// First, read the variable number of input bytes into a fixed size buffer.
		enum { maxBytes = (maxBits + 6) / 7 };
		uint8 bytes[maxBytes] = {0};
		uintp numBytes = 0;
		int8 signExtendShift = (int8)sizeof(Value) * 8;
		while(numBytes < maxBytes)
		{
			uint8 byte = *stream.advance(1);
			bytes[numBytes] = byte;
			++numBytes;
			signExtendShift -= 7;
			if(!(byte & 0x80)) { break; }
		};

		// Ensure that the input does not encode more than maxBits of data.
		enum { numUsedBitsInHighestByte = maxBits - (maxBytes-1) * 7 };
		enum { highestByteUsedBitmask = uint8(1<<numUsedBitsInHighestByte)-1 };
		enum { highestByteSignedBitmask = ~uint8(highestByteUsedBitmask) & ~uint8(0x80) };
		if((bytes[maxBytes-1] & ~highestByteUsedBitmask) != 0
		&& ((bytes[maxBytes-1] & ~highestByteUsedBitmask) != uint8(highestByteSignedBitmask) || !std::is_signed<Value>::value))
		{ throw FatalSerializationException("Invalid LEB encoding: invalid final byte"); }

		// Decode the buffer's bytes into the output integer.
		value = 0;
		for(uintp byteIndex = 0;byteIndex < maxBytes;++byteIndex)
		{ value |= Value(bytes[byteIndex] & ~0x80) << (byteIndex * 7); }
		
		// Sign extend the output integer to the full size of Value.
		if(std::is_signed<Value>::value && signExtendShift > 0)
		{ value = (value << signExtendShift) >> signExtendShift; }

		// Check that the output integer is in the expected range.
		if(value < minValue || value > maxValue)
		{ throw FatalSerializationException(std::string("out-of-range value: ") + std::to_string(minValue) + "<=" + std::to_string(value) + "<=" + std::to_string(maxValue)); }
	}

	// Helpers for various common LEB128 parameters.
	template<typename Stream,typename Value> void serializeVarUInt1(Stream& stream,Value& value) { serializeVarInt<Value,1>(stream,value,0,1); }
	template<typename Stream,typename Value> void serializeVarUInt7(Stream& stream,Value& value) { serializeVarInt<Value,7>(stream,value,0,127); }
	template<typename Stream,typename Value> void serializeVarUInt32(Stream& stream,Value& value) { serializeVarInt<Value,32>(stream,value,0,UINT32_MAX); }
	template<typename Stream,typename Value> void serializeVarUInt64(Stream& stream,Value& value) { serializeVarInt<Value,64>(stream,value,0,UINT64_MAX); }
	template<typename Stream,typename Value> void serializeVarInt32(Stream& stream,Value& value) { serializeVarInt<Value,32>(stream,value,INT32_MIN,INT32_MAX); }
	template<typename Stream,typename Value> void serializeVarInt64(Stream& stream,Value& value) { serializeVarInt<Value,64>(stream,value,INT64_MIN,INT64_MAX); }

	// Serializes a constant. If deserializing, throws a FatalSerializationException if the deserialized value doesn't match the constant.
	template<typename Constant>
	void serializeConstant(InputStream& stream,const char* constantMismatchMessage,Constant constant)
	{
		Constant savedConstant;
		serialize(stream,savedConstant);
		if(savedConstant != constant)
		{
			throw FatalSerializationException(std::string(constantMismatchMessage) + ": loaded " + std::to_string(savedConstant) + " but was expecting " + std::to_string(constant));
		}
	}
	template<typename Constant>
	void serializeConstant(OutputStream& stream,const char* constantMismatchMessage,Constant constant)
	{
		serialize(stream,constant);
	}

	// Serialize containers.
	template<typename Stream>
	void serialize(Stream& stream,std::string& string)
	{
		size_t size = string.size();
		serializeVarUInt32(stream,size);
		if(Stream::isInput) { string.resize(size); }
		serializeBytes(stream,(uint8*)string.c_str(),size);
		if(Stream::isInput) { string.shrink_to_fit(); }
	}

	template<typename Stream,typename Element,typename Allocator,typename SerializeElement>
	void serializeArray(Stream& stream,std::vector<Element,Allocator>& vector,SerializeElement serializeElement)
	{
		size_t size = vector.size();
		serializeVarUInt32(stream,size);
		if(Stream::isInput) { vector.resize(size); }
		for(uintp index = 0;index < vector.size();++index) { serializeElement(stream,vector[index]); }
		if(Stream::isInput) { vector.shrink_to_fit(); }
	}

	template<typename Stream,typename Element,typename Allocator>
	void serialize(Stream& stream,std::vector<Element,Allocator>& vector)
	{
		serializeArray(stream,vector,[](Stream& stream,Element& element){serialize(stream,element);});
	}
}