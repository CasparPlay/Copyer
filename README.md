# Copyer
copyer is a program written to do copy based on commands sent from CasparPlay client. It meant to copy files from razuna asset management path to CasparCG server media path. CasparCG server media path should be shared and should be mounted on Linux. To run copyer, first compile the program with the following command -

          gcc -Wall -o copyer copyer.c -lpthread
          
Then just run the program as: 

        ./copyer -d "media path of the mounted casparcgserver"
