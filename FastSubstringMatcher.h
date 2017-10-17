/*
 * FastSubstringMatcher.h
 *
 *  Created on: Sep 11, 2014
 *      Author: kalujny
 */

#ifndef FASTSUBSTRINGMATCHER_H_
#define FASTSUBSTRINGMATCHER_H_

#include <vector>
#include <string>
#include <map>
#include <stdint.h>

 // used to quickly match input against set of strings up to 256 chars long. 17% slower than handwritten nested switches
class FastSubstringMatcher
{
public:
	FastSubstringMatcher(const FastSubstringMatcher & other);
	FastSubstringMatcher();
	~FastSubstringMatcher();
	FastSubstringMatcher & operator=(const FastSubstringMatcher & other);
	void SetKeywords(const std::vector < std::string > & keywords); // PASS KEYWORDS IN MOST FREQUENT ORDER
	inline bool MatchSubstring(char* pBegin, char* pEnd, size_t& matchIndex) const
	{
		const uint16_t matchSize = pEnd - pBegin;
		if (spotSelectorMatch[matchSize] != 0)
		{
			std::vector < MatcherSpot > * pMatcher = spotSelectorMatch[matchSize];
			std::vector < MatcherSpot >::const_iterator it = pMatcher->begin();
			do
			{
				if (it->daChar == pBegin[it->pos])
				{
					if (it->matchingStringIdx >= 0)
					{
						matchIndex = static_cast<size_t>(it->matchingStringIdx);
						return true;
					}
				}
				else if (it->matchingStringIdx < 0)
				{
					it += -1 * it->matchingStringIdx;
				}
			} while (++it != pMatcher->end());
		}
		return false;
	}

	inline bool MatchSubstring(char* pBegin, char* pEnd, const char*& pMatch) const
	{
		const uint16_t matchSize = pEnd - pBegin;
		if (maxKeywordSize >= matchSize && spotSelectorMatch[matchSize] != 0)
		{
			std::vector < MatcherSpot > * pMatcher = spotSelectorMatch[matchSize];
			std::vector < MatcherSpot >::const_iterator it = pMatcher->begin();
			do
			{
				if (it->daChar == pBegin[it->pos])
				{
					if (it->matchingStringIdx >= 0)
					{
						pMatch = keywords[it->matchingStringIdx].c_str();
						return true;
					}
				}
				else if (it->matchingStringIdx < 0)
				{
					it += -1 * it->matchingStringIdx;
				}
			} while (++it != pMatcher->end());
		}
		return false;
	}

	struct MatcherSpot
	{
		unsigned char pos;
		char daChar;
		int16_t matchingStringIdx;
		MatcherSpot(unsigned char pos_ = 0, char daChar_ = 'x', uint16_t matchingStringIdx_ = -1) : pos(pos_), daChar(daChar_), matchingStringIdx(matchingStringIdx_) {}
	};
private:
	void solveMatchers(
		const std::vector < std::pair < uint16_t, std::string > > &   in_curSizeMatches,
		const uint16_t &                                              curStringSize,
		std::vector < MatcherSpot > &                               in_curMatcher);

	static const uint16_t HOLY_GRAIL_SIZE = 64;

	char padding0[HOLY_GRAIL_SIZE]; // protect matcher from false sharing
	uint16_t maxKeywordSize;
	std::map < uint16_t, std::vector < MatcherSpot > * > spotSelector;
	std::vector < MatcherSpot > * spotSelectorMatch[256];
	std::vector < std::string > keywords;
	std::map < std::string, uint16_t > reverseMatchKeywords;
	char padding1[HOLY_GRAIL_SIZE]; // protect matcher from false sharing
};

// IDEA BELOW (surely some bearded guy thought of that before):
// EXAMPLE: 0=fxa, 1=fza, 2=aab, 3=aaa, 4=cab, 5=eab -> [[0,f,-1],[1,x,0],[1,z,1],[0,a,-1],[2,b,2],[2,a,3],[0,c,4],[0,e,5]]
// To construct one spree ( example above ) from currentSizeMatches
// 1. compute number of different chars at every position among candidates
// 2. select leftmost position with largest different chars
// 3. for every different char at selected position ( top to bottom to maintain expected frequency )
	// 3.1 create a subset of currentSizeMatches which matches on previous step
	// 3.2 if subset size is 1, we got a match!
		// 3.2.1 add MatcherSpot into sequence: pos from step 2, char for step 4 ( no preference on order ), return result on match OR magic value on miss/continue
	// 3.3 zero out chars at previously selected char position
	// 3.4 repeat step 2->end

#endif /* FASTSUBSTRINGMATCHER_H_ */
