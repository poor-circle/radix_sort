#pragma once
#include <vector>
#include <array>
#include <execution>
#include <future>
#include <memory>
#include <cstring>
#include <limits>

template <typename T>
class radix_trait;

template <typename T>
class radix_trait_greater;

template<size_t i, typename Trait, typename Iter, typename cnt_type>
void count_duff_device(Iter begin, Iter end, std::array<cnt_type, std::numeric_limits<unsigned char>::max() + 1 >& counter)
{
	if constexpr (i > 0)
		memset(counter.data(), 0, sizeof(counter));
	auto length = std::distance(begin, end);
	if (length == 0)
		return;
	//We should take care of iterator to avoid it out range of [begin,end], or we may face assertion in debug mode
	auto diff = (length % 4 + 3) % 4;
	begin += diff;
	end -= 1;
	switch (diff) //Dear duff device
	{
		for (;; begin += 4)
		{
	[[fallthrough]]; case 3:++counter[Trait::template get<i>(*(begin - 3))];
	[[fallthrough]]; case 2:++counter[Trait::template get<i>(*(begin - 2))];
	[[fallthrough]]; case 1:++counter[Trait::template get<i>(*(begin - 1))];
	[[fallthrough]]; case 0:++counter[Trait::template get<i>(*begin)];
							if (begin == end) [[unlikely]]
								break;
		}
	};
}

template<size_t i, typename Trait, typename Iter, typename cnt_type>
void place_duff_device(Iter raw, typename std::iterator_traits<Iter>::value_type* buffer, std::array<cnt_type, std::numeric_limits<unsigned char>::max() + 1 >& counter, size_t pos_beg, size_t pos_end)
{
	if constexpr (i % 2 == 0)
		raw += pos_beg;
	else
		buffer += pos_beg;
	std::ptrdiff_t j_raw = pos_end - pos_beg, j = j_raw - (j_raw % 4);
	unsigned char t0, t1, t2, t3;
	if constexpr (i % 2 == 0)
	{
		switch (j_raw % 4)
		{
			for (; j >= 0; j -= 4)
			{
				t3 = Trait::template get<i>(raw[j + 3]);
				buffer[--counter[t3]] = std::move(raw[j + 3]);
		[[fallthrough]]; case 3:	t2 = Trait::template get<i>(raw[j + 2]);
			buffer[--counter[t2]] = std::move(raw[j + 2]);
		[[fallthrough]]; case 2:	t1 = Trait::template get<i>(raw[j + 1]);
			buffer[--counter[t1]] = std::move(raw[j + 1]);
		[[fallthrough]]; case 1:	t0 = Trait::template get<i>(raw[j]);
			buffer[--counter[t0]] = std::move(raw[j]);
		[[fallthrough]]; case 0:;
			}
		}
	}
	else
	{
		switch (j_raw % 4)
		{
			for (; j >= 0; j -= 4)
			{
				t3 = Trait::template get<i>(buffer[j + 3]);
				raw[--counter[t3]] = std::move(buffer[j + 3]);
		[[fallthrough]]; case 3:	t2 = Trait::template get<i>(buffer[j + 2]);
			raw[--counter[t2]] = std::move(buffer[j + 2]);
		[[fallthrough]]; case 2:	t1 = Trait::template get<i>(buffer[j + 1]);
			raw[--counter[t1]] = std::move(buffer[j + 1]);
		[[fallthrough]]; case 1:	t0 = Trait::template get<i>(buffer[j]);
			raw[--counter[t0]] = std::move(buffer[j]);
		[[fallthrough]]; case 0:;
			}
		}
	}
}

template <size_t i, typename Trait, typename Iter, typename cnt_type>
void radix_sort_impl(Iter first, Iter second, typename std::iterator_traits<Iter>::value_type* buffer, std::array<cnt_type, std::numeric_limits<unsigned char>::max() + 1 >& counter)
{
	using namespace std;
	constexpr auto radix_size = Trait::radix_size;

	if constexpr (i < radix_size)
	{
		count_duff_device<i, Trait>(first, second, counter);
		for (size_t j = 1; j <= numeric_limits<unsigned char>::max(); ++j)
			counter[j] += counter[j - 1];
		place_duff_device<i, Trait>(first, buffer, counter, 0, distance(first, second));
	}
	if constexpr (i != radix_size - 1)
	{
		radix_sort_impl<i + 1, Trait, Iter>(first, second, buffer, counter);
	}
	else if constexpr (radix_size % 2)
	{
		std::move(buffer, buffer + std::distance(first, second), first);
	}
}

template <size_t i, typename Iter, typename Trait, typename cnt_type>
void parallel_radix_sort_impl(Iter first, Iter second, unsigned int thrd_lim, typename std::iterator_traits<Iter>::value_type* buffer, std::array<cnt_type, std::numeric_limits<unsigned char>::max() + 1 >* counter)
{
	using namespace std;
	constexpr auto radix_size = Trait::radix_size;
	auto length = distance(first, second), parallel_width = length / thrd_lim;
	if constexpr (i < radix_size)
	{
		{
			vector<future<void>> wait_works;
			wait_works.reserve(thrd_lim - 1);
			auto j = 0u;
			for (; j < thrd_lim - 1; ++j)
			{
				auto beg = j * parallel_width, end = beg + parallel_width;
				if constexpr (i % 2 == 0)
					wait_works.emplace_back(async(launch::async, count_duff_device<i, Trait, Iter, cnt_type>, first + beg, first + end, std::ref(counter[j])));
				else
					wait_works.emplace_back(async(launch::async, count_duff_device<i, Trait, typename std::iterator_traits<Iter>::value_type*, cnt_type>, buffer + beg, buffer + end, std::ref(counter[j])));
			}
			auto beg = j * parallel_width;
			if constexpr (i % 2 == 0)
				count_duff_device<i, Trait>(first + beg, first + length, counter[j]);
			else
				count_duff_device<i, Trait>(buffer + beg, buffer + length, counter[j]);
		}

		for (int j = thrd_lim - 2; j >= 0; --j)
			for (size_t k = 0; k <= numeric_limits<unsigned char>::max(); ++k)
				counter[j][k] += counter[j + 1][k];
		for (size_t j = 1; j <= numeric_limits<unsigned char>::max(); ++j)
			counter[0][j] += counter[0][j - 1];
		for (auto j = 1u; j < thrd_lim; ++j)
			for (size_t k = 0; k <= numeric_limits<unsigned char>::max(); ++k)
				counter[j][k] = counter[0][k] - counter[j][k];

		vector<future<void>> wait_works;
		wait_works.reserve(thrd_lim - 1);
		auto j = 0u;
		for (; j < thrd_lim - 1; ++j)
		{
			auto beg = j * parallel_width, end = beg + parallel_width;
			wait_works.emplace_back(async(launch::async, place_duff_device<i, Trait, Iter, cnt_type>, first, buffer, ref(counter[j + 1]), beg, end));
		}
		place_duff_device<i, Trait>(first, buffer, counter[0], j * parallel_width, length);
	}
	if constexpr (i != radix_size - 1)
	{
		parallel_radix_sort_impl<i + 1, Iter, Trait>(first, second, thrd_lim, buffer, counter);
	}
	else if constexpr (radix_size % 2)
	{
		std::move(buffer, buffer + length, first);
	}
}

template <typename Trait, typename Iter>
void parallel_radix_sort(Iter first, Iter second, typename std::iterator_traits<Iter>::value_type* buffer = nullptr, unsigned int thrd_lim = std::thread::hardware_concurrency())
{
	using namespace std;
	using value_type = typename iterator_traits<Iter>::value_type;
	constexpr auto thrd_sort_length_limit = 100000; //each thread's min sort length
	auto length = distance(first, second);
	thrd_lim = min<decltype(length)>(thrd_lim, max<decltype(length)>(1u, length / thrd_sort_length_limit));
	unique_ptr<unsigned char[]> resource;
	if (buffer == nullptr)
	{
		resource = make_unique<unsigned char[]>(length * sizeof(value_type));
		buffer = (value_type*)resource.get();
	}
	if (thrd_lim > 1)
	{
		//cnt type optimize seems negative in multi-thread?
		auto counter = make_unique<array<decltype(length), numeric_limits<unsigned char>::max() + 1 >[]>(thrd_lim);
		parallel_radix_sort_impl<0, Iter, Trait>(first, second, thrd_lim, buffer, counter.get());

	}
	else
	{
		if (length <= INT_MAX) //int is enough for counter
		{
			auto counter = make_unique<array<int, numeric_limits<unsigned char>::max() + 1 >>();
			radix_sort_impl<0, Trait, Iter>(first, second, buffer, *counter);
		}
		else
		{
			auto counter = make_unique<array<decltype(length), numeric_limits<unsigned char>::max() + 1 >>();
			radix_sort_impl<0, Trait, Iter>(first, second, buffer, *counter);
		}
	}
}

template <typename Iter>
void parallel_radix_sort(Iter first, Iter second, typename std::iterator_traits<Iter>::value_type* buffer = nullptr, unsigned int thrd_lim = std::thread::hardware_concurrency())
{
	parallel_radix_sort<radix_trait<typename std::iterator_traits<Iter>::value_type>>(first, second, buffer, thrd_lim);
}

template <typename Trait, typename Iter>
void radix_sort(Iter first, Iter second, typename std::iterator_traits<Iter>::value_type* buffer = nullptr)
{
	using namespace std;
	using value_type = typename iterator_traits<Iter>::value_type;
	auto length = distance(first, second);
	unique_ptr<unsigned char[]> resource;
	if (buffer == nullptr)
	{
		resource = make_unique<unsigned char[]>(length * sizeof(value_type));
		buffer = (value_type*)resource.get();
	}
	if (length <= INT_MAX) //int is enough for counter 
	{
		auto counter = make_unique<array<int, numeric_limits<unsigned char>::max() + 1 >>();
		radix_sort_impl<0, Trait, Iter>(first, second, buffer, *counter);
	}
	else
	{
		auto counter = make_unique<array<decltype(length), numeric_limits<unsigned char>::max() + 1 >>();
		radix_sort_impl<0, Trait, Iter>(first, second, buffer, *counter);
	}
}

template <typename Iter>
void radix_sort(Iter first, Iter second, typename std::iterator_traits<Iter>::value_type* buffer = nullptr)
{
	radix_sort<radix_trait<typename std::iterator_traits<Iter>::value_type>>(first, second, buffer);
}

template <typename Iter, typename ExecutionPolicy>
void radix_sort(Iter first, Iter second, ExecutionPolicy&& policy, typename std::iterator_traits<Iter>::value_type* buffer = nullptr)
{
	if constexpr (std::is_same_v<std::remove_cvref_t<ExecutionPolicy>, std::execution::parallel_policy> || std::is_same_v<std::remove_cvref_t<ExecutionPolicy>, std::execution::parallel_unsequenced_policy>)
		parallel_radix_sort<radix_trait<typename std::iterator_traits<Iter>::value_type>>(first, second, buffer);
	else
		radix_sort<radix_trait<typename std::iterator_traits<Iter>::value_type>>(first, second, buffer);
}

template <typename Trait, typename Iter, typename ExecutionPolicy>
void radix_sort(Iter first, Iter second, ExecutionPolicy&& policy, typename std::iterator_traits<Iter>::value_type* buffer = nullptr)
{
	if constexpr (std::is_same_v<std::remove_cvref_t<ExecutionPolicy>, std::execution::parallel_policy> || std::is_same_v<std::remove_cvref_t<ExecutionPolicy>, std::execution::parallel_unsequenced_policy>)
		parallel_radix_sort<Trait>(first, second, buffer);
	else
		radix_sort<Trait>(first, second, buffer);
}


template <typename T>
struct radix_trait<T*>
{
	static constexpr std::size_t radix_size = sizeof(T*) / sizeof(unsigned char);
	template <size_t index>
	static unsigned char get(T* const & obj) noexcept
	{
		static_assert(index < radix_size);
		return ((unsigned char*)&obj)[index];
	}
};

template <typename T>
struct radix_trait_greater<T*>
{
	static constexpr std::size_t radix_size = sizeof(T) / sizeof(unsigned char);
	template <size_t index>
	static unsigned char get(T* const& obj) noexcept
	{
		static_assert(index < radix_size);
		return ~((unsigned char*)&obj)[index];
	}
};

template <std::unsigned_integral T>
struct radix_trait<T>
{
	static constexpr std::size_t radix_size = sizeof(T) / sizeof(unsigned char);
	template <size_t index>
	static unsigned char get(const T& obj) noexcept
	{
		static_assert(index < radix_size);
		return ((unsigned char*)&obj)[index];
	}
};

template <std::unsigned_integral T>
struct radix_trait_greater<T>
{
	static constexpr std::size_t radix_size = sizeof(T) / sizeof(unsigned char);
	template <size_t index>
	static unsigned char get(const T& obj) noexcept
	{
		static_assert(index < radix_size);
		return ~((unsigned char*)&obj)[index];
	}
};


template <typename T>
	requires std::signed_integral<T> || std::floating_point<T>
struct radix_trait<T>
{
	static constexpr std::size_t radix_size = sizeof(T) / sizeof(unsigned char);
	template <size_t index>
	static unsigned char get(const T& obj) noexcept
	{
		static_assert(index < radix_size);
		if constexpr (index < radix_size - 1)
			return ((unsigned char*)&obj)[index];
		else
			return ((unsigned char*)&obj)[index] ^ 0x80u;
	}
};

template <typename T>
	requires std::signed_integral<T> || std::floating_point<T>
struct radix_trait_greater<T>
{
	static constexpr std::size_t radix_size = sizeof(T) / sizeof(unsigned char);
	template <size_t index>
	static unsigned char get(const T& obj) noexcept
	{
		static_assert(index < radix_size);
		if constexpr (index < radix_size - 1)
			return ~((unsigned char*)&obj)[index];
		else
			return ~(((unsigned char*)&obj)[index] ^ 0x80u);
	}
};

template<typename p1, typename p2>
struct radix_trait<std::pair<p1, p2>>
{
	static constexpr std::size_t radix_size = (sizeof(p1)+sizeof(p2)) / sizeof(unsigned char);
	template <size_t index>
	static unsigned char get(const std::pair<p1, p2>& obj) noexcept
	{
		static_assert(index < radix_size);
		if constexpr (index < sizeof(p2))
			return radix_trait<p2>::template get<index>(obj.second);
		else
			return radix_trait<p1>::template get<index - sizeof(p2)>(obj.first);
	}
};
