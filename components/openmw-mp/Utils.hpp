#ifndef UTILS_HPP
#define UTILS_HPP

#include <algorithm>
#include <cstdint>
#include <string>
#include <sstream>
#include <vector>

#include <components/esm/attr.hpp>
#include <components/esm3/activespells.hpp>
#include <components/esm3/loadcell.hpp>
#include <components/esm3/loadmgef.hpp>
#include <components/esm3/loadskil.hpp>

#if (defined __WIN32__ || defined _WIN32 || defined WIN32)
#define __WINDOWS
#endif

#ifdef __WINDOWS
int setenv(const char *name, const char *value, int overwrite);
#endif

namespace Utils
{
    std::string convertPath(std::string str);
    bool doesFileHaveChecksum(std::string filePath, unsigned int requiredChecksum);

    void timestamp();

    int progressFunc(double TotalToDownload, double NowDownloaded);

    unsigned int getNumberOfDigits(int integer);
    bool compareDoubles(double a, double b, double epsilon = 0.005);
    bool compareFloats(float a, float b, float epsilon = 0.005f);

    template <class Type, class Type2>
    bool vectorContains(const std::vector<Type> &vectorChecked, const Type2 &value)
    {
        return std::find(vectorChecked.begin(), vectorChecked.end(), value) != vectorChecked.end();
    }

    template <class Type>
    uint32_t getVectorSize(const std::vector<Type> &vectorChecked)
    {
        return static_cast<uint32_t>(vectorChecked.size());
    }

    template <class Type>
    void resetVector(std::vector<Type> &vectorInput, uint32_t newSize)
    {
        vectorInput.clear();
        vectorInput.resize(newSize);
    }

    std::string replaceString(const std::string &source, const char *find, const char *replace);

    std::string toString(int num);

    std::string &removeExtension(std::string &file);

    long int getFileLength(const char *file);

    unsigned int crc32Checksum(const std::string &file);

    std::string getOperatingSystemType();
    std::string getArchitectureType();

    std::string getVersionInfo(std::string appName, std::string version, std::string commitHash, int protocol);

    void printWithWidth(std::ostringstream &sstr, std::string str, size_t width);

    std::string intToHexStr(unsigned val);
    unsigned int hexStrToInt(std::string hexString);

    inline std::string getCellDescription(const ESM::Cell &cell)
    {
        if (cell.isExterior())
            return std::to_string(cell.mData.mX) + ", " + std::to_string(cell.mData.mY);
        return cell.mName;
    }

    inline int32_t activeEffectArgToInt(const ESM::ActiveEffect &effect)
    {
        if (const ESM::RefId *id = std::get_if<ESM::RefId>(&effect.mArg))
        {
            if (id->empty())
                return -1;
            int32_t attr = ESM::Attribute::refIdToIndex(*id);
            if (attr != -1)
                return attr;
            return ESM::Skill::refIdToIndex(*id);
        }
        else if (const ESM::RefNum *actor = std::get_if<ESM::RefNum>(&effect.mArg))
        {
            if (actor->isSet())
                return static_cast<int32_t>(actor->mIndex);
        }
        return -1;
    }

    inline void intToActiveEffectArg(ESM::ActiveEffect &effect, int32_t arg)
    {
        if (arg < 0)
        {
            effect.mArg = ESM::RefId();
            return;
        }
        if (effect.mEffectId == ESM::MagicEffect::DrainAttribute ||
            effect.mEffectId == ESM::MagicEffect::DamageAttribute ||
            effect.mEffectId == ESM::MagicEffect::RestoreAttribute ||
            effect.mEffectId == ESM::MagicEffect::FortifyAttribute ||
            effect.mEffectId == ESM::MagicEffect::AbsorbAttribute)
        {
            if (arg < ESM::Attribute::Length)
            {
                effect.mArg = ESM::Attribute::indexToRefId(arg);
                return;
            }
        }
        else if (effect.mEffectId == ESM::MagicEffect::DrainSkill ||
                 effect.mEffectId == ESM::MagicEffect::DamageSkill ||
                 effect.mEffectId == ESM::MagicEffect::RestoreSkill ||
                 effect.mEffectId == ESM::MagicEffect::FortifySkill ||
                 effect.mEffectId == ESM::MagicEffect::AbsorbSkill)
        {
            if (arg < ESM::Skill::Length)
            {
                effect.mArg = ESM::Skill::indexToRefId(arg);
                return;
            }
        }
        effect.mArg = ESM::RefNum{ static_cast<uint32_t>(arg), -2 };
    }
}
#endif //UTILS_HPP
