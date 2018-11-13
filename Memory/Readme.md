use `memory_coordinator [input]` to execute this memory scheudler. 

There are three stages in this schedule algorithm:
1. Recognize which virtual machine is lack memory and which virtual machine has left memory
I used `unused memory` as a criterion to distinguish between these two categories. If `unused_memory < CONSUMER_RATIO * total_memory`, this virtual machine belongs to consumer category(requires memory). If `unused_memory > PROVIDER_RATIO * total_memory`, this virtual machine belongs to provider category(release memory). For middle range, they do not get or release memory. 
2. provider give memory to consumer
We need to calculate the total need by consumer, and for each provider we give a `RELEASE_RATIO` for them to give up certain amount of memory propotional to their `available_memory`. 
3. If provider cannot provide enough memory for consumer, we need to extract memory from domain. 
First we need to check whether domain has enough free memory. If it does, then release free memory to these machines. If not, domain will not release meory. The total memory released will be shared all consumers. 
