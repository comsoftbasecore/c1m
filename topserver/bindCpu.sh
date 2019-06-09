treeStr="|----"
usage(){
echo "./`basename $0` [set|list]"
echo 
echo "set:按队列数从0号CPU开始依次绑定"
echo "list:列出个网卡队列情况"
echo
exit
}
if [ "$1" != "" ]
then
    cmdtype=$1
else
    cmdtype="list"
fi
if [ "$cmdtype" != "set" -a "$cmdtype" != "list" ]
then
usage
fi

#扩展：比如需要将多个队列绑定在一个cpu上，
#入参是绑定的队列数：如3就是每个cpu上面绑定3个队列；若是列表，列表的个数表示预计使用的cpu数（也有可能少于设置的数据当队列数少于参数总和时）如 3 2 2 则表示cpu0绑定3个队列，cpu1绑定2个队列，cpu2绑定2个(和剩余的所有队列)
#实现：加一个参数，程序顺序计算绑定的cpu编号；或者一个参数列表，队列按列表顺序绑定cpu，若参数总和多于队列数则忽略多余的，若参数综合少于队列数，剩余的全绑定在最后一个


echo
#此处有坑，不同的系统可能会不一样，根据情况修改
#作用是找出网络设备
#此处ifconfig的结果格式为：p6p2: flags=4163<UP,BROADCAST,RUNNING,MULTICAST>  mtu 1500
#还遇到到过格式为：eth0      Link encap:Ethernet  HWaddr 90:B1:1C:35:5E:14 
for dev in `ifconfig |grep ": "|grep -v "lo" |awk -F':' '{print $1}'`
do
    cpuFlag=1
    echo "Dev:"$dev
    for devseq in `grep $dev /proc/interrupts |awk '{print $1}'|sed -e 's/://g'`
      do
      echo  "/proc/irq/${devseq}/smp_affinity"
    if [ "$cmdtype" = "set" ]
    then
      echo `printf %0x ${cpuFlag}` >/proc/irq/${devseq}/smp_affinity
    fi
    sleep 0.5
    echo -n $treeStr$devseq 
    cat /proc/irq/${devseq}/smp_affinity
    ((cpuFlag=cpuFlag*2))
  done
    echo 
done