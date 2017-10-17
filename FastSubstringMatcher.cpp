/*
 * FastSubstringMatcher.cpp
 *
 *  Created on: Sep 11, 2014
 *      Author: kalujny
 */

#include <algorithm>
#include <functional>
#include <string.h>

#include "FastSubstringMatcher.h"

#include <iostream>

struct MatchBySpot
{
	MatchBySpot(uint16_t pos, char daChar)
	{
		this->pos = pos;
		this->daChar = daChar;
	}
	bool operator()(const std::pair < uint16_t, std::string > & daIndexedString)
	{
		return daIndexedString.second.at(pos) == daChar;
	}
private:
	uint16_t pos;
	char daChar;
};

struct MismatchBySpot
{
	MismatchBySpot(uint16_t pos, char daChar)
	{
		this->pos = pos;
		this->daChar = daChar;
	}
	bool operator()(const std::pair < uint16_t, std::string > & daIndexedString)
	{
		bool res = daIndexedString.second.at(pos) != daChar;
		return res;
	}
private:
	uint16_t pos;
	char daChar;
};

bool operator==(const FastSubstringMatcher::MatcherSpot & lhs, const FastSubstringMatcher::MatcherSpot & rhs)
{
	return ((lhs.pos == rhs.pos)
		&& (lhs.daChar == rhs.daChar)
		&& (lhs.daChar == rhs.daChar)
		&& (lhs.matchingStringIdx == rhs.matchingStringIdx));
}

bool spotLessThan(const std::pair <uint16_t, std::vector < FastSubstringMatcher::MatcherSpot > > & lhs, const std::pair <uint16_t, std::vector < FastSubstringMatcher::MatcherSpot > > & rhs)
{
	return lhs.first < rhs.first;
}

bool varianceGreaterThan(const std::pair< uint16_t, std::vector < char > > & lhs, const std::pair< uint16_t, std::vector < char > > & rhs)
{
	if (lhs.second.size() == rhs.second.size())
		return lhs.first < rhs.first;
	return lhs.second.size() > rhs.second.size();
}

struct SizeNotEqualTo
{
	SizeNotEqualTo(uint16_t targetSize)
	{
		this->targetSize = targetSize;
	}
	bool operator()(const std::string & daString)
	{
		return daString.size() != targetSize;
	}
private:
	uint16_t targetSize;
};

void FastSubstringMatcher::solveMatchers(
	const std::vector < std::pair < uint16_t, std::string > > &   currentMatches,
	const uint16_t &                                              curStringSize,
	std::vector < MatcherSpot > &                               curMatcher)
{
	uint16_t curResult = (uint16_t)int16_t(-1);

	std::vector < std::pair < unsigned char, std::vector < char > > > diffCharsAtPos;
	std::vector < char > charsAtPos;
	for (unsigned char j = 0; j < curStringSize; ++j)
	{
		charsAtPos.clear();
		for (std::vector < std::pair < uint16_t, std::string > >::const_iterator it = currentMatches.begin(); it != currentMatches.end(); ++it)
			charsAtPos.push_back(it->second.at(j));
		std::vector < char > uniqCharsAtPos;
		for (char nextChar : charsAtPos)
		{
			if (std::find(uniqCharsAtPos.begin(), uniqCharsAtPos.end(), nextChar) == uniqCharsAtPos.end())
				uniqCharsAtPos.push_back(nextChar);
		}
		diffCharsAtPos.push_back(std::pair< unsigned char, std::vector < char > >(j, uniqCharsAtPos));
	}
	std::sort(diffCharsAtPos.begin(), diffCharsAtPos.end(), varianceGreaterThan);

	unsigned char curIdx = diffCharsAtPos[0].first;

	for (uint16_t k = 0; k < diffCharsAtPos[0].second.size(); ++k)
	{
		char curChar = diffCharsAtPos[0].second[k];
		std::vector < std::pair < uint16_t, std::string > > curCharMatches = currentMatches;
		curCharMatches.erase(std::remove_if(curCharMatches.begin(), curCharMatches.end(), MismatchBySpot(curIdx, curChar)), curCharMatches.end());
		if (curCharMatches.size() == 1)
		{
			// got a match!
			curResult = curCharMatches[0].first;
			curMatcher.push_back(MatcherSpot(curIdx, curChar, curResult));

		}
		else
		{
			uint16_t toSkip = static_cast<uint16_t>(curCharMatches.size());
			curMatcher.push_back(MatcherSpot(curIdx, curChar, -1 * toSkip));
			for (std::vector < std::pair < uint16_t, std::string > >::iterator it = curCharMatches.begin(); it != curCharMatches.end(); ++it)
				it->second.at(curIdx) = '0';
			// set new values and RECURSE the beach
			std::vector < std::pair < uint16_t, std::string > > newcurCharMatches = curCharMatches;
			solveMatchers(newcurCharMatches, curStringSize, curMatcher);
		}
	}
}

// PASS KEYWORDS IN DESCENDING FREQUENCY
void FastSubstringMatcher::SetKeywords(const std::vector < std::string > & matchKeywords)
{
	// reset inner structures
	spotSelector.clear();
	keywords = matchKeywords;

	// prefill spotSelector with match lengths, fill reverse map
	maxKeywordSize = 1;
	for (uint16_t i = 0; i < keywords.size(); ++i)
	{
		reverseMatchKeywords[keywords[i]] = i;
		if (spotSelector.find(static_cast<uint16_t>(keywords[i].size())) == spotSelector.end())
		{
			spotSelector.insert(std::pair<uint16_t, std::vector < MatcherSpot > * >(static_cast<uint16_t>(keywords[i].size()), new std::vector < MatcherSpot >()));
			if (maxKeywordSize < static_cast<uint16_t>(keywords[i].size()))
				maxKeywordSize = static_cast<uint16_t>(keywords[i].size());
		}

	}

	++maxKeywordSize;

	// construct matching spree and complete spot selector
	for (std::map < uint16_t, std::vector < MatcherSpot > * >::iterator it = spotSelector.begin(); it != spotSelector.end(); ++it)
	{
		std::vector < MatcherSpot > & curMatcher = *(it->second);
		std::vector < std::string > currentSizeMatches = keywords;
		currentSizeMatches.erase(std::remove_if(currentSizeMatches.begin(), currentSizeMatches.end(), SizeNotEqualTo(it->first)), currentSizeMatches.end());
		std::vector < std::pair < uint16_t, std::string > > currentMatches(currentSizeMatches.size());
		for (uint16_t j = 0; j < currentSizeMatches.size(); ++j)
			currentMatches[j] = std::pair < uint16_t, std::string >(reverseMatchKeywords[currentSizeMatches[j]], currentSizeMatches[j]);

		solveMatchers(currentMatches, it->first, curMatcher);
	}
	for (auto it : spotSelector)
		spotSelectorMatch[it.first] = it.second;
}

FastSubstringMatcher::FastSubstringMatcher(const FastSubstringMatcher & other)
{
	for (std::map < uint16_t, std::vector < MatcherSpot > * >::const_iterator it = other.spotSelector.begin(); it != other.spotSelector.end(); ++it)
	{
		this->spotSelector.insert(std::pair<uint16_t, std::vector < MatcherSpot > * >(it->first, new std::vector < MatcherSpot >()));
		*(this->spotSelector[it->first]) = *(it->second);
	}
	this->maxKeywordSize = other.maxKeywordSize;
	this->keywords = other.keywords;
	this->reverseMatchKeywords = other.reverseMatchKeywords;
}

FastSubstringMatcher::FastSubstringMatcher()
{
	this->maxKeywordSize = 0;
	memset(spotSelectorMatch, 0, sizeof(spotSelectorMatch));
}

FastSubstringMatcher::~FastSubstringMatcher()
{
	for (std::map < uint16_t, std::vector < MatcherSpot > * >::iterator it = spotSelector.begin(); it != spotSelector.end(); ++it)
		delete it->second;
}

FastSubstringMatcher & FastSubstringMatcher::operator=(const FastSubstringMatcher & other)
{
	for (std::map < uint16_t, std::vector < MatcherSpot > * >::iterator it = this->spotSelector.begin(); it != this->spotSelector.end(); ++it)
		delete it->second;
	this->spotSelector.clear();
	for (std::map < uint16_t, std::vector < MatcherSpot > * >::const_iterator it = other.spotSelector.begin(); it != other.spotSelector.end(); ++it)
	{
		this->spotSelector.insert(std::pair<uint16_t, std::vector < MatcherSpot > * >(it->first, new std::vector < MatcherSpot >()));
		*(this->spotSelector[it->first]) = *(it->second);
	}
	this->keywords = other.keywords;
	this->reverseMatchKeywords = other.reverseMatchKeywords;
	return *this;
}
