#include <sys/stat.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <algorithm>

// #define MEMORY 4
// #define IDX_SIZE MEMORY * 1024 * 1024 // 4 * 1024^2
#define IDX_SIZE 6000000
#define IDX_RUN IDX_SIZE / (98 * 4) 
#define IDX_ARR_SIZE IDX_RUN * 98
#define C_OFFSET IDX_ARR_SIZE - 98

using namespace std;

fstream bwt;
fstream idx;
int idx_run; // how many runs of counts
int bwt_size;
int fetch_size;
int C_offset;
vector<int> idx_arr;
vector<int> printed_record;
char * buffer;

int char2offset(char c)
{
    switch (c) {
    case 9:
        return 0;
    case 10:
        return 1;
    case 13:
        return 2;
    default:
        return c - 29;
    }
}

void bucketInc(vector<int>& C, char c)
{
    switch (c) {
    case 9:
        C[0] += 1;
        break;
    case 10:
        C[1] += 1;
        break;
    case 13:
        C[2] += 1;
        break;
    default:
        C[c - 29] += 1;
    }
}

/*
scan bwt file and construct idx (including the final C)
ASCII 9->0, 10->1, 13->2,  32->3 ... 126->97
note that position 0 stores /09's occ
use 4 Bytes to represent a number
counting once yields 98*4=392 Byte
suppose a maximum of 4MB memory is used for idx
4MB = 4*1024*1024 Byte
hence, could count 4MB/392Byte=10699 times, including C

-bwt_size > IDX_SIZE (>4M) : external idx, fetch_size >= 392, idx_run = 10699
    read every fetch_size and deal with remainder
    e.g. x|x|xx| or xxxx|xxxx|xxxxxxx|

-bwt_size <= IDX_SIZE (<4M) : internal idx, idx_run = min(bwt_size, 10699);
    -bwt_size > idx_run (>10699B): fetch_size >=1
        read every fetch_size and deal with remainder
        e.g. x|x|xx| or xxxx|xxxx|xxxxxxx| same as above, just samller
    -bwt_size <= idx_run (<=10699B): fetch_size = 0
        read all bwt in one bwt.read
        e.g. x|x|x|x|

the last index is actually the C
is constructed by a running sum
during the running sum, char's not in the file still got a count,
but this won't affect later comupation
*/
void makeExtIdx()
{
    vector<int> C(98, 0);
    bwt.seekg(0, bwt.beg);
    int remainder_size = bwt_size - (IDX_RUN - 1) * fetch_size;
    int i,j;
    // remainder size always larger than fetch size
    // deal with fetch_size in every run
    for (i = 0; i < IDX_RUN - 1; ++i) {
        // deal with every fetch size
        bwt.read(buffer, fetch_size);
        for (j = 0; j < fetch_size; ++j)
            bucketInc(C, buffer[j]);
        // serialize C to idx
        for (j = 0; j < 98; ++j)
            idx.write((char*)(&C[j]), 4);
    }
    // remainder
    bwt.read(buffer, remainder_size);
    for (i = 0; i < remainder_size; ++i)
        bucketInc(C, buffer[i]);
    // final C
    int running_sum = 0;
    int tmp;
    for (i = 0; i < 98; ++i) {
        tmp = C[i];
        C[i] = running_sum;
        idx.write((char*)(&C[i]), 4);
        running_sum += tmp;
    }
}

void makeIntIdx()
{
    vector<int> C(98, 0);
    bwt.seekg(0, bwt.beg);
    int i;
    int j;
    if (bwt_size > idx_run) { // idx on every fetch size
        int remainder_size = bwt_size - (idx_run - 1) * fetch_size;
        for (i = 0; i < idx_run - 1; ++i) {
            bwt.read(buffer, fetch_size);
            for (j = 0; j < fetch_size; ++j)
                bucketInc(C, buffer[j]);
            for (j = 0; j < 98; ++j)
                idx_arr[i * 98 + j] = C[j];
        }
        // remainder
        bwt.read(buffer, remainder_size);
        for (i = 0; i < remainder_size; ++i)
            bucketInc(C, buffer[i]);
        for (i = 0; i < 98; ++i)
            idx_arr[(idx_run - 1) * 98 + i] = C[i];
    }
    else { // idx on every char
        char* buffer = new char[bwt_size];
        bwt.read(buffer, bwt_size);
        for (i = 0; i < bwt_size; ++i) {
            bucketInc(C, buffer[i]);
            for (j = 0; j < 98; ++j)
                idx_arr[i * 98 + j] = C[j];
        }
    }
    // final C
    int running_sum = 0;
    int tmp;
    for (i = 0; i < 98; ++i) {
        tmp = idx_arr[C_offset + i];
        idx_arr[C_offset + i] = running_sum;
        running_sum += tmp;
    }
}

/*
at position pos (inlcuding), how many c has occurred in bwt
*/
int occ(char c, int pos)
{
    if (pos == 0)
        return 0;
    switch (fetch_size) {
    case 0: // every pos is idx'ed x|x|x|x|
        if (pos == bwt_size) { // can't use the last idx, use the one before
            int occ = idx_arr[(pos - 2) * 98 + char2offset(c)];
            char tmp;
            bwt.seekg(pos - 1);
            bwt.get(tmp);
            if (tmp == c)
                occ++;
            return occ;
        }
        else { // pos k, use idx k-1, which is gain by (k-2)*98+offset
            return idx_arr[(pos - 1) * 98 + char2offset(c)];
        }
    default: // x|x|xx| or xxxx|xxxx|xxxxxxx|
        int base_run = pos / fetch_size;
        // can't use the last idx, use the one before and scan
        base_run = (base_run >= IDX_RUN) ? (IDX_RUN - 1) : base_run;
        int scan_start_pos = base_run * fetch_size;
        int occ = (base_run == 0) ? 0 : idx_arr[(base_run - 1) * 98 + char2offset(c)];
        bwt.seekg(scan_start_pos);
        bwt.read(buffer, pos - scan_start_pos);
        for (int i = 0; i < pos - scan_start_pos; ++i) {
            if (buffer[i] == c)
                occ++;
        }
        return occ;
    }
}

/* the pos of delta'th c in the bwt file */
int invOcc(char c, int delta)
{
    switch (fetch_size) {
    case 0: // every pos is idx'ed x|x|x|x|
        if (delta == idx_arr[char2offset(c)]) { // must be the first char
            return 1;
        }
        else { // binary search on the idx space
            // in fact, delta here won't equal to bwt_size(idx_run)
            int lo = 0;
            int hi = idx_run - 2; // last run of index is a running sum
            int mid = (lo + hi + 1) / 2;
            while (hi > lo) {
                if (idx_arr[mid * 98 + char2offset(c)] < delta) {
                    lo = mid;
                }
                else {
                    hi = mid - 1;
                }
                mid = (lo + hi + 1) / 2;
            }
            return mid + 2;
        }
    default: // x|x|xx| or xxxx|xxxx|xxxxxxx|
        if (delta <= idx_arr[char2offset(c)]) { // c appears before idx #0, scan from bwt.beg
            bwt.seekg(0, bwt.beg);
            bwt.read(buffer, fetch_size);
            int occ = 0;
            int i;
            for (i = 0; i < fetch_size; ++i) {
                if (buffer[i] == c)
                    occ++;
                if (occ == delta)
                    break;
            }
            return i + 1;
        }
        else { // binary search on the idx space, find the lower bound idx and start scanning
            // in fact, delta here won't equal to bwt_size(idx_run)
            int lo = 0;
            int hi = IDX_RUN - 2; // last run of index is a running sum
            int mid = (lo + hi + 1) / 2;
            while (hi > lo) {
                if (idx_arr[mid * 98 + char2offset(c)] < delta) {
                    lo = mid;
                }
                else {
                    hi = mid - 1;
                }
                mid = (lo + hi + 1) / 2;
            }
            int occ = idx_arr[mid * 98 + char2offset(c)];
            int pos = (mid + 1) * fetch_size;
            if (mid == IDX_RUN - 2) {
                int remainder_size = bwt_size - (IDX_RUN - 1) * fetch_size;
                bwt.seekg(pos);
                bwt.read(buffer, remainder_size);
                int i;
                for (i = 0; i < remainder_size; ++i) {
                    if (buffer[i] == c)
                        occ++;
                    if (occ == delta)
                        break;
                }
                return pos + i + 1;
            }
            else {
                bwt.seekg(pos);
                bwt.read(buffer, fetch_size);
                int i;
                for (i = 0; i < fetch_size; ++i) {
                    if (buffer[i] == c)
                        occ++;
                    if (occ == delta)
                        break;
                }
                return pos + i + 1;
            }
        }
    }
}

string backDecode(int index)
{
    string result = "";
    bool hasSeenRightBracket = false;
    char tmpChar;
    bwt.seekg(index - 1);
    bwt.get(tmpChar);
    result += tmpChar;
    if (tmpChar == ']')
        hasSeenRightBracket = true;
    while (tmpChar != '[') {
        index = idx_arr[C_offset + char2offset(tmpChar)] + occ(tmpChar, index);
        bwt.seekg(index - 1);
        bwt.get(tmpChar);
        result += tmpChar;
        if (tmpChar == ']')
            hasSeenRightBracket = true;
    }
    if (hasSeenRightBracket) {
        vector<int>::iterator ret;
        ret = find(printed_record.begin(), printed_record.end(), index);
        if (ret == printed_record.end()) {
            printed_record.push_back(index);
            return result;
        }
    }
    return "";
}

string forwardDecode(int index)
{
    string result = "";
    char c = ' ';
    int delta;
    int lo;
    int hi;
    int mid;
    while (c != '[') {
        // binary search find highest that has a number less than index
        lo = C_offset;
        hi = C_offset + 97;
        mid = (lo + hi + 1) / 2;
        while (hi > lo) {
            if (idx_arr[mid] < index)
                lo = mid;
            else
                hi = mid - 1;
            mid = (lo + hi + 1) / 2;
        }
        switch (mid - C_offset) {
        case 0:
            c = 9;
            break;
        case 1:
            c = 10;
            break;
        case 2:
            c = 13;
            break;
        default:
            c = mid - (C_offset - 29);
        }
        result += c;
        delta = index - idx_arr[C_offset + char2offset(c)];
        index = invOcc(c, delta);
    }
    return result;
}

int main(int argc, char* argv[])
{
    int i;
    bwt.open(argv[1], fstream::in);
    streampos begin = bwt.tellg();
    bwt.seekg(0, bwt.end);
    streampos end = bwt.tellg();
    bwt_size = end - begin; // bwt file size in bytes
    fetch_size = bwt_size / int(IDX_RUN); // fetch how many bytes each time when making idx

    // create idx
    if (bwt_size > IDX_SIZE) { // use external idx
        // check idx exsitence
        struct stat buff;
        bool idx_exists = (stat(argv[2], &buff) == 0);
        // init
        idx_arr.resize(IDX_ARR_SIZE, 0);
        C_offset = C_OFFSET;
        // see if idx exists, if not then create. restore anyway for search.
        buffer = new char[bwt_size - (IDX_RUN - 1) * fetch_size];
        if (!idx_exists) {
            idx.open(argv[2], fstream::out);
            makeExtIdx();
            idx.close();
        }
        idx.open(argv[2], fstream::in);
        for (i = 0; i < IDX_ARR_SIZE; ++i)
            idx.read((char*)(&idx_arr[i]), 4);
        idx.close();
    }
    else { // use in-memory idx, same size as the external one
        idx_run = bwt_size < IDX_RUN ? bwt_size : IDX_RUN;
        buffer = new char[bwt_size - (idx_run - 1) * fetch_size];
        idx_arr.resize(idx_run * 98, 0);
        C_offset = (idx_run - 1) * 98;
        makeIntIdx();
    }

    // get search terms
    vector<string> terms(argv + 3, argv + argc);

    // check [
    string tmp = terms[0];
    for (i = 1; i < terms.size(); ++i)
        tmp += terms[i];
    size_t found = tmp.find('[');
    if (found != string::npos) // find [
        return 0;

    /*
backward search to find index
then decode to the left first to determin if it is valid (against '[')
if not empty, then decode the remaining part

for multi-term search, conbine parts into candidate, perform std::find
*/

    // backward search, take the smallest range
    string P;
    char c;
    int First = 0;
    int Last = 167772160; // 160*1024*1024
    int First2;
    int Last2;
    int First3;
    int Last3;
    int selected;
    switch(terms.size()){
        case 3:
            P = terms[2];
            i = P.size() - 1;
            c = P[i];
            First = idx_arr[C_offset + char2offset(c)] + 1;
            Last = idx_arr[C_offset + char2offset(c) + 1];
            while ((First <= Last) and (i > 0)) {
                c = P[i - 1];
                First = idx_arr[C_offset + char2offset(c)] + occ(c, First - 1) + 1;
                Last = idx_arr[C_offset + char2offset(c)] + occ(c, Last);
                i--;
            }
            selected = 2;
        case 2:
            P = terms[1];
            i = P.size() - 1;
            c = P[i];
            First2 = idx_arr[C_offset + char2offset(c)] + 1;
            Last2 = idx_arr[C_offset + char2offset(c) + 1];
            while ((First2 <= Last2) and (i > 0)) {
                c = P[i - 1];
                First2 = idx_arr[C_offset + char2offset(c)] + occ(c, First2 - 1) + 1;
                Last2 = idx_arr[C_offset + char2offset(c)] + occ(c, Last2);
                i--;
            }
            if (Last2-First2 < Last-First)
            {
                First = First2;
                Last = Last2;
                selected = 1;
            }
        case 1:
            P = terms[0];
            i = P.size() - 1;
            c = P[i];
            First3 = idx_arr[C_offset + char2offset(c)] + 1;
            Last3 = idx_arr[C_offset + char2offset(c) + 1];
            while ((First3 <= Last3) and (i > 0)) {
                c = P[i - 1];
                First3 = idx_arr[C_offset + char2offset(c)] + occ(c, First3 - 1) + 1;
                Last3 = idx_arr[C_offset + char2offset(c)] + occ(c, Last3);
                i--;
            }
            if (Last3-First3 < Last-First)
            {
                First = First3;
                Last = Last3;
                selected = 0;
            }
    }

    // bidirectional decoding
    string left;
    string right;
    string candidate;
    size_t bracket;
    for (i = First; i < Last + 1; ++i) {
         left = backDecode(i);
        if (!left.empty()) {
            reverse(left.begin(), left.end());
            right = forwardDecode(i);
            candidate = left + right.substr(0, right.size() - 1);
            bracket = left.find(']');
            // check other terms' existence
            switch (terms.size()) {
            case 3:
                switch(selected){
                    case 2:
                        found = candidate.find(terms[0], bracket+1);
                        if (found == string::npos)
                            continue;
                        found = candidate.find(terms[1], bracket+1);
                        if (found == string::npos)
                            continue;
                        cout << candidate << endl;
                        break;
                    case 1:
                        found = candidate.find(terms[0], bracket+1);
                        if (found == string::npos)
                            continue;
                        found = candidate.find(terms[2], bracket+1);
                        if (found == string::npos)
                            continue;
                        cout << candidate << endl;
                        break;
                    default: // selected = 0
                        found = candidate.find(terms[1], bracket+1);
                        if (found == string::npos)
                            continue;
                        found = candidate.find(terms[2], bracket+1);
                        if (found == string::npos)
                            continue;
                        cout << candidate << endl;
                        break;
                }
                break;
            case 2:
                switch(selected){
                    case 1:
                        found = candidate.find(terms[0], bracket+1);
                        if (found == string::npos)
                            continue;
                        cout << candidate << endl;
                        break;
                    default: // selected = 0
                        found = candidate.find(terms[1], bracket+1);
                        if (found == string::npos)
                            continue; // continue the for loop
                        cout << candidate << endl;
                        break;
                }
                break;
            default: // term size = 1
                cout << candidate << endl;
                break;
            }
        }
    }

    delete [] buffer;
    bwt.close();
}