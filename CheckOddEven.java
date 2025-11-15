
import java.util.Scanner;
public class CheckOddEven {
   
    public static void main(String arg[])
    {
         int a;
         Scanner sc = new Scanner(System.in);
         a = sc.nextInt();
         System.out.println("the value of a is : "+a);
         if(a%2==0)
         {
            System.out.println("the number is even number");

         }
         else{
            System.out.println("the number is odd number");
         }


    }

}
