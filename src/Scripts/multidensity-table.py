import csv
import sys

# As a hack to get sorted output, sort input with:
# (head -n 1 multidensity.csv && tail -n +2 multidensity.csv | sort) > multidensity-temp.csv

# TODO: take this from BitFunnel instead of hardcoding.
input_size = 1297.97

filename = "/tmp/multidensity-temp.csv"
with open(filename) as f:
    reader = csv.reader(f)
    header = next(reader)
    # assert header == ['Treatment','Density', 'Bytes', 'SNR', 'QPS']
    assert header == ['Treatment','Density', 'Bytes', 'QPS']
    header[-2] = 'Size (MB)'
    header[-1] = 'kQPS'
    header.append('DQ')
    writer = csv.writer(sys.stdout, delimiter= '&')

    header[-1] += "\\\\"
    writer.writerow(header)

    last_treatment = ""
    for row in reader:
    #     if row[0] == "Rank0":
    #         row[1] = "N/A"
    #     elif row[0] == "RankN":
    #         if row[1] == "0":
    #             row[1] = "DQ"
    #         elif row[1] == "1":
    #             row[1] = "Q"
    #         elif row[1] == "2":
    #             row[1] = "D"
    #     else:
    #         print("Error: expected Rank0 or RankN treatment")
    #         assert False
        if row[0] == "ClassicBitsliced":
            row[0] = "BitslicedSignature"
        elif row[0] == "PrivateSharedRank0":
            row[0] = "FreqConscious"
        elif row[0] == "Optimal":
            row[0] = "GeneralizedBlocked"
        if row[0] != last_treatment:
            print("\\hline")
            last_treatment = row[0]

        # row[-4] = str(float(row[-4]) / 1297.97)
        row[-2] = str(float(row[-2]) * 651587 / 1000000)

        dq = "{0:.0f}".format(float(row[-1]) / float(row[-2]))
        row.append(dq)

        # convert from qps to kqps.
        row[-2] = "{0:.0f}".format(float(row[-2])/1000)

        row[-3] = "{0:.0f}".format(float(row[-3]))

        row[-1] += "\\\\"
        writer.writerow(row)
